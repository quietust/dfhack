// Dwarf Name Manager - retrieves names from Twitch.TV chat

#include "Core.h"
#include "Console.h"
#include "Export.h"
#include "PluginManager.h"
#include "MiscUtils.h"
#include "modules/Screen.h"
#include "modules/Gui.h"
#include "modules/Translation.h"
#include "modules/Units.h"
#include "modules/EventManager.h"

#include "tinythread.h"
#include "ActiveSocket.h"
#include "json/json.h"

#include <vector>
#include <string>
#include <set>
#include <queue>
#include <algorithm>
#include <tuple>
#include <random>

#include "VTableInterpose.h"
#include "df/world.h"
#include "df/ui.h"
#include "df/graphic.h"
#include "df/viewscreen_customize_unitst.h"
#include "df/interface_key.h"
#include "df/unit.h"
#include "df/unit_soul.h"
#include "df/historical_entity.h"

#include "uicommon.h"
#include "listcolumn.h"

using std::set;
using std::map;
using std::vector;
using std::queue;
using std::string;
using std::wstring;

using namespace DFHack;
using namespace df::enums;

DFHACK_PLUGIN("twitchname");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);
REQUIRE_GLOBAL(world);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(gps);

struct TwitchInfo
{
	int id;
	string nickname;
	string dispname;
	bool is_sub;
	bool is_mod;
	bool is_online;

	operator std::string ()
	{
		string msg = "#" + std::to_string(id) + ": " + dispname + " (" + nickname + "), " + (is_mod ? 'M' : 'm') + (is_sub ? 'S' : 's') + (is_online ? 'O' : 'o');
		return msg;
	}
};

// Server Config
namespace config
{
	string channel;
	int track_name_changes;
	int announce_name_changes;
	int debug_level;
}

// Client State
namespace state
{
	map<int, int> units;
	map<int, int> twitches;
}

void _sleep (int mil)
{
#ifdef _WIN32
	Sleep(mil);
#else
	usleep(mil * 1000);
#endif
}

namespace chat
{
	enum COMMAND_TYPE { COMMAND_NONE, COMMAND_CONNECT, COMMAND_CLOSE, COMMAND_TERMINATE };

	typedef tthread::lock_guard<tthread::mutex> lock;
	tthread::thread *thread;
	tthread::mutex mutex;
	COMMAND_TYPE command;
	volatile int reply;
	volatile bool ready;
	string error_msg;

	map<string, int> nicknames;
	map<int, TwitchInfo> users;
	set<int> name_changes;
	queue<string> debug_log;

	void writeSock (CActiveSocket &sock, string data)
	{
		data += "\r\n";
		size_t len = data.size();
		int ret = sock.Send((uint8 *)data.c_str(), len);
		if (ret == -1)
			throw std::runtime_error(string("Write error: ") + sock.DescribeError());
		if (ret == 0)
			throw std::runtime_error("Connection interrupted");
		if (ret != len)
			throw std::runtime_error("writeSock failed to transmit entire request");
	}

	void readSock (CActiveSocket &sock, vector<string> &tokens, map<string,string> &tags)
	{
		string buf;
		tokens.clear();
		tags.clear();
		while (true)
		{
			unsigned char chr;
			int ret = sock.Receive(1, &chr);
			if (ret == -1)
				throw std::runtime_error(string("Read error: ") + sock.DescribeError());
			if (ret == 0)
				throw std::runtime_error("Connection interrupted");
			if (chr == '\r')
				continue;
			else if (chr == '\n')
				break;
			else	buf.push_back(chr);
		}
		if (!buf.size())
			return;
		std::stringstream ss(buf);
		while (std::getline(ss, buf, ' '))
			tokens.push_back(buf);
		// tags present? extract them
		if (tokens[0][0] == '@')
		{
			buf = tokens[0].substr(1);
			tokens.erase(tokens.begin());
			ss.clear();
			ss.str(buf);
			while (std::getline(ss, buf, ';'))
			{
				size_t pos = buf.find('=');
				if (pos != std::string::npos)
					tags[buf.substr(0, pos)] = buf.substr(pos + 1);
			}
		}
		// strip leading colon from first argument
		if (tokens[0][0] == ':')
			tokens[0] = tokens[0].substr(1);
		for (int i = 0; i < tokens.size(); i++)
		{
			// merge final argument together
			if (tokens[i][0] == ':')
			{
				tokens[i] = tokens[i].substr(1);
				for (int j = i + 1; j < tokens.size(); j++)
					tokens[i] += " " + tokens[j];
				tokens.resize(i + 1);
				break;
			}
		}
	}

	void debug_msg (int level, const string &message, const vector<string> &tokens = vector<string>(), const map<string,string> &tags = map<string,string>())
	{
		if (config::debug_level < level)
			return;
		string msg = message;
		if (tokens.size())
		{
			msg += " [";
			for (auto iter = tokens.begin(); iter != tokens.end(); iter++)
				msg += *iter + " ";
			msg.pop_back();
			msg += "]";
		}
		if (tags.size())
		{
			msg += " {";
			for (auto iter = tags.begin(); iter != tags.end(); iter++)
				msg += " '" + iter->first + "'='" + iter->second + "',";
			msg.pop_back();
			msg += " }";
		}
		debug_log.push(msg);
	}

	void threadProc (void *param)
	{
		CActiveSocket sock(CSimpleSocket::SocketTypeTcp);
		ready = false;
		vector<string> tokens;
		map<string,string> tags;

		while (true)
		{
			_sleep(1);
			try
			{
				switch (command)
				{
				case COMMAND_NONE:
					break;
				case COMMAND_CONNECT:
					if (!sock.Initialize())
						throw std::runtime_error(string("Connect error: ") + sock.DescribeError());
					if (!sock.Open("irc.chat.twitch.tv", 6667))
						throw std::runtime_error(string("Connect error: ") + sock.DescribeError());
					{
						// pick random nickname
						std::random_device rd;
						std::minstd_rand gen(rd());
						std::uniform_int_distribution<> dis(100000,999999);
						writeSock(sock, "NICK justinfan" + to_string(dis(gen)));
					}
					while (true)
					{
						// wait for end of MOTD
						readSock(sock, tokens, tags);
						if (tokens.size() > 1 && tokens[1] == "376")
							break;
					}
					// request extra capabilities
					writeSock(sock, "CAP REQ :twitch.tv/tags twitch.tv/membership twitch.tv/commands");
					readSock(sock, tokens, tags);
					if (tokens[1] != "CAP" || tokens[3] != "ACK")
						throw std::runtime_error("Failed to request chat capabilities");

					// join the channel
					writeSock(sock, "JOIN #" + config::channel);
					while (true)
					{
						lock _lock(mutex);
						readSock(sock, tokens, tags);
						debug_msg(1, "Joining channel", tokens, tags);
						// NAMES reply
						if (tokens.size() > 5 && tokens[1] == "353")
						{
							std::stringstream ss(tokens[5]);
							string name;
							while (std::getline(ss, name, ' '))
							{
								// if it's not in the list, it'll set it to 0
								// which we can detect later as being "not known"
								if (!nicknames[name])
									continue;
								int userid = nicknames[name];
								if (users.count(userid))
									users[userid].is_online = true;
							}
						}
						// End of NAMES
						else if (tokens.size() > 1 && tokens[1] == "366")
							break;
					}
					reply = 1;
					ready = true;
					break;
				case COMMAND_CLOSE:
					if (ready)
						writeSock(sock, "QUIT");
					sock.Close();
					ready = false;
					reply = 1;
					break;
				case COMMAND_TERMINATE:
					ready = false;
					sock.Close();
					reply = 1;
					return;
				}
				command = COMMAND_NONE;
			}
			catch (std::runtime_error e)
			{
				sock.Close();
				error_msg = e.what();
				ready = false;
				reply = -1;
				command = COMMAND_NONE;
			}
			catch (...)
			{
				sock.Close();
				error_msg = "Unknown exception while processing command";
				ready = false;
				reply = -1;
				command = COMMAND_NONE;
			}
			if (!ready)
				continue;
			try
			{
				// Can't use this, because Select(...) checks for read AND write,
				// which makes it ALWAYS return immediately (and is totally useless)
//				if (!sock.Select(0, 1))
//					continue;
				{
					// Reimplement it here, to do what I actually need it to do
					SOCKET s = sock.GetSocketDescriptor();
					struct timeval timeout = { 0, 0 };
					fd_set fds;
					FD_ZERO(&fds);
					FD_SET(s, &fds);
					int r = SELECT(s + 1, &fds, NULL, NULL, &timeout);
					if (r < 0)
						throw std::runtime_error("SELECT failed on socket");
					if (!r)
						continue;
				}
				readSock(sock, tokens, tags);
				if (tokens.size() > 1 && tokens[0] == "PING")
					writeSock(sock, "PONG " + tokens[1]);
				else if (tokens.size() > 2 && (tokens[1] == "PRIVMSG" || tokens[1] == "USERNOTICE"))
				{
					if (tokens[2].substr(1) != config::channel)
						continue;
					lock _lock(mutex);
					debug_msg(3, "User spoke", tokens, tags);
					string nickname;
					if (tokens[1] == "USERNOTICE")
						nickname = tags["login"];
					else	nickname = tokens[0].substr(0, tokens[0].find('!'));
					string dispname = tags["display-name"];
					if (!dispname.size())
					{
						debug_msg(4, "User had no dispname, using nickname '" + nickname + "' instead");
						dispname = nickname;
					}
					int userid = stoi(tags["user-id"]);
					bool is_mod = stoi(tags["mod"]) != 0;
					bool is_sub = stoi(tags["subscriber"]) != 0;
					nicknames[nickname] = userid;

					auto &user = users[userid];
					user.id = userid;
					user.nickname = nickname;

					if (user.dispname != dispname)
					{
						debug_msg(4, "User '"+user.dispname+"' changed name to '" + dispname + "'");
						user.dispname = dispname;
						name_changes.insert(userid);
					}
					user.is_sub = is_sub;
					user.is_mod = is_mod;
					user.is_online = true;
					debug_msg(4, "Updated user info: " + string(user));
				}
				else if (tokens.size() > 2 && tokens[1] == "JOIN")
				{
					if (tokens[2].substr(1) != config::channel)
						continue;
					lock _lock(mutex);
					debug_msg(2, "User joined", tokens, tags);
					const string &nickname = tokens[0].substr(0, tokens[0].find('!'));
					// assign to 0 if not present
					if (!nicknames[nickname])
					{
						debug_msg(4, "Joining user '"+nickname+"' is not known, ignoring but remembering as online");
						continue;
					}
					int userid = nicknames[nickname];
					if (users.count(userid))
					{
						auto &user = users[userid];
						user.is_online = true;
						debug_msg(4, "Joining user '"+nickname+"' is " + string(user) + " - now Online");
					}
					else	debug_msg(4, "Joining user '"+nickname+"' is #" + std::to_string(userid) + ", not known");
				}
				else if (tokens.size() > 2 && tokens[1] == "PART")
				{
					if (tokens[2].substr(1) != config::channel)
						continue;
					lock _lock(mutex);
					debug_msg(2, "User left", tokens, tags);
					const string &nickname = tokens[0].substr(0, tokens[0].find('!'));
					// do NOT set to 0 if not present
					if (!nicknames.count(nickname))
					{
						debug_msg(4, "Leaving user '"+nickname+"' is not known, ignoring");
						continue;
					}
					// if it's present and zero, then delete it
					if (!nicknames[nickname])
					{
						debug_msg(4, "Leaving user '"+nickname+"' was not known, forgetting");
						nicknames.erase(nickname);
						continue;
					}
					int userid = nicknames[nickname];
					if (users.count(userid))
					{
						auto &user = users[userid];
						debug_msg(4, "Leaving user '"+nickname+"' is " + string(user) + " - now Offline");
						user.is_online = false;
					}
					else	debug_msg(4, "Leaving user '"+nickname+"' is #" + std::to_string(userid) + ", not known");
				}
				else if (tokens.size() > 4 && tokens[1] == "MODE")
				{
					if (tokens[2].substr(1) != config::channel)
						continue;
					lock _lock(mutex);
					debug_msg(2, "User mode changed", tokens, tags);
					const string &nickname = tokens[4];
					const string &mode = tokens[3];
					if (mode == "+o")
					{
						// set to 0 if not present
						if (!nicknames[nickname])
						{
							debug_msg(4, "Mode changed user '"+nickname+"' is not known, ignoring but remembering as online");
							continue;
						}
						int userid = nicknames[nickname];
						if (!users.count(userid))
						{
							debug_msg(4, "Mode changed user '"+nickname+"' is #" + std::to_string(userid) + ", not known");
							continue;
						}
						auto &user = users[userid];
						user.is_mod = true;
						user.is_online = true;
						debug_msg(4, "Mode changed user '"+nickname+"' is " + string(user) + " - now Moderator and Online");
					}
					if (mode == "-o")
					{
						// do NOT set to 0 if not present
						if (!nicknames.count(nickname) || !nicknames[nickname])
						{
							debug_msg(4, "Mode changed user '"+nickname+"' is not known, ignoring");
							continue;
						}
						int userid = nicknames[nickname];
						if (!users.count(userid))
						{
							debug_msg(4, "Mode changed user '"+nickname+"' is #" + std::to_string(userid) + ", not known");
							continue;
						}
						auto &user = users[userid];
						if (user.is_online)
						{
							debug_msg(4, "Mode changed user '"+nickname+"' is " + string(user) + " - now Not Moderator");
							user.is_mod = false;
						}
						else	debug_msg(4, "Mode changed user '"+nickname+"' is " + string(user) + " - not online, so ignoring");
					}
				}
			}
			catch (std::runtime_error e)
			{
				sock.Close();
				error_msg = e.what();
				ready = false;
			}
			catch (...)
			{
				sock.Close();
				error_msg = "Unknown exception while processing event";
				ready = false;
			}
		}
	}

}

class viewscreen_twitchnamest : public dfhack_viewscreen {
public:
	void feed(set<df::interface_key> *events);
	void render();

	string getFocusString() { return "twitchname"; }

	viewscreen_twitchnamest(df::unit *unit);
	~viewscreen_twitchnamest() { };

protected:
	ListColumn<TwitchInfo *> choices;
	TwitchInfo *cur_sel;
	df::unit *unit;
	bool failed_random;
};

string getOrdinalStr (int num)
{
	if (num == 1)
		return "";
	string suffix = " the " + std::to_string(num);
	switch (num % 100)
	{
	case 1:	case 21:case 31:case 41:case 51:case 61:case 71:case 81:case 91:
		suffix += "st";
		break;
	case 2:	case 22:case 32:case 42:case 52:case 62:case 72:case 82:case 92:
		suffix += "nd";
		break;
	case 3:	case 23:case 33:case 43:case 53:case 63:case 73:case 83:case 93:
		suffix += "rd";
		break;
	default:
		suffix += "th";
		break;
	}
	return suffix;
}

int getOrdinalNum (const string &name)
{
	size_t off1 = name.rfind(' ');
	if ((off1 == std::string::npos) || (off1 == 0) || (off1 == name.size() - 1))
		return 1;
	size_t off2 = name.rfind(' ', off1 - 1);
	if ((off2 == std::string::npos) || (off2 == 0) || (off2 == off1 - 1))
		return 1;
	if (name.substr(off2 + 1, off1 - off2 - 1) != "the")
		return 1;
	int num = stoi(name.substr(off1 + 1));
// extra sanity check, if we really want it
//	if (name.substr(off2) != getOrdinalStr(num))
//		return 1;
	return num;
}

bool assignUnit (TwitchInfo *user, df::unit *unit)
{
	if (state::twitches.count(user->id))
	{
		// unassign previous unit, but only if it's not dead
		int unit_id = state::twitches[user->id];
		df::unit *old_unit = df::unit::find(unit_id);
		if (old_unit && !Units::isDead(old_unit))
		{
			Units::setNickname(old_unit, "");
			state::units.erase(unit_id);
			state::twitches.erase(user->id);
		}
	}
	// assign new unit
	if (unit)
	{
		if (!Units::isDead(unit))
		{
			state::twitches[user->id] = unit->id;
			// count up how many times this user has been assigned in the past
			int suffix = 1;
			for (auto iter = state::units.begin(); iter != state::units.end(); iter++)
			{
				int unit_id = iter->first;
				int twitch_id = iter->second;
				if ((unit_id != unit->id) && (twitch_id == user->id))
					suffix++;
			}
			string name = UTF2DF(user->dispname) + getOrdinalStr(suffix);

			Units::setNickname(unit, name.c_str());
		}
		state::units[unit->id] = user->id;
	}
	else
	{
		// when unassigning a unit, check if anyone else was assigned and relink the first one
		// this is solely for the "previous dwarf is dead" indicator
		for (auto iter = state::units.begin(); iter != state::units.end(); iter++)
		{
			int unit_id = iter->first;
			int twitch_id = iter->second;
			if (twitch_id == user->id)
			{
				state::twitches[twitch_id] = unit_id;
				break;
			}
		}
		
	}
	return true;
}

// NOTE: must be called from main thread while holding the Chat Mutex lock
bool checkFixName (df::unit *unit, int twitch_id, string *outmsg = NULL, bool doFix = true)
{
	string msg;
	if (!chat::users.count(twitch_id))
		return false;	// this should be impossible
	const TwitchInfo &user = chat::users[twitch_id];

	string old_name = unit->name.nickname;
	int ordinal = getOrdinalNum(old_name);

	string new_name = UTF2DF(user.dispname) + getOrdinalStr(ordinal);
	if (new_name != old_name)
	{
		msg = unit->name.first_name + " " + Translation::TranslateName(&unit->name, false, true);
		msg[0] = toupper(msg[0]);
		msg += " has changed ";
		if (unit->sex == 1)
			msg += "his";
		else if (unit->sex == 0)
			msg += "her";
		else	msg += "its";
		msg += " nickname from '" + old_name + "' to '" + new_name + "'";
		if (outmsg)
			*outmsg = msg;
		if (config::announce_name_changes)
			Gui::showAnnouncement(msg, 6, true);
		if (doFix)
			Units::setNickname(unit, new_name.c_str());
		return true;
	}
	return false;
}

bool saveState ()
{
	chat::lock _lock(chat::mutex);

	Json::Value dwarves;
	for (auto iter = state::units.begin(); iter != state::units.end(); iter++)
	{
		int unit_id = iter->first;
		int twitch_id = iter->second;
		dwarves[to_string(unit_id)] = Json::Value(twitch_id);
	}

	Json::Value twitch;
	for (auto iter = chat::users.begin(); iter != chat::users.end(); iter++)
	{
		int twitch_id = iter->first;
		TwitchInfo &info = iter->second;

		auto &user = twitch[to_string(twitch_id)];
		user["nickname"] = Json::Value(info.nickname);
		user["dispname"] = Json::Value(info.dispname);
		user["is_sub"] = Json::Value(info.is_sub);
		user["is_mod"] = Json::Value(info.is_mod);
	}

	Json::Value data;
	data["channel"] = config::channel;
	data["dwarves"] = dwarves;
	data["twitch"] = twitch;

	Json::FastWriter writer;
	std::ofstream file;
	file.open("data/save/current/twitchname.dat", std::ios_base::trunc);
	if (!file.is_open())
		return false;
	file << writer.write(data);
	file.close();
	return true;
}

bool loadState(string &channel)
{
	Json::Reader reader;
	std::ifstream file;
	file.open("data/save/current/twitchname.dat");
	if (!file.is_open())
	{
		string path = "data/save/" + (df::global::world->cur_savegame.save_dir) + "/twitchname.dat";
		file.open(path);
	}
	// if there's no data, treat it as success - it's probably a new fortress
	if (!file.is_open())
		return true;

	Json::Value data;
	if (!reader.parse(file, data))
		return false;

	// data too old
	if (!data.isMember("channel"))
		return false;

	channel = data["channel"].asString();
	// wrong channel
	if (data["channel"] != config::channel)
		return false;

	chat::lock _lock(chat::mutex);

	Json::Value twitch = data["twitch"];
	Json::Value::Members names = twitch.getMemberNames();
	for (auto iter = names.begin(); iter != names.end(); iter++)
	{
		int twitch_id = stoi(*iter);
		auto &user = twitch[*iter];
		const string &nickname = user["nickname"].asString();

		// do we already have live chat data for this user? if so, ignore what was in the savegame
		if (chat::users.count(twitch_id))
		{
			chat::debug_msg(4, "Ignored savegame data for user #" + std::to_string(twitch_id) + ", already loaded from server");
			continue;
		}

		TwitchInfo &info = chat::users[twitch_id];
		info.id = twitch_id;
		info.nickname = nickname;
		info.dispname = user["dispname"].asString();
		info.is_sub = user["is_sub"].asBool();
		info.is_mod = user["is_mod"].asBool();
		info.is_online = false;

		// populate nickname lookup info - if it's not present, then add it
		if (!chat::nicknames.count(nickname))
		{
			chat::debug_msg(4, "Nickname '" + nickname + "' registered as #" + std::to_string(info.id));
			chat::nicknames[nickname] = info.id;
		}
		// if it's present and zero, then set it nonzero and mark the user as Online
		else if (!chat::nicknames[nickname])
		{
			chat::debug_msg(4, "Nickname '" + nickname + "' registered as #" + std::to_string(info.id) + " and marked as online");
			chat::nicknames[nickname] = info.id;
			info.is_online = true;
		}
	}

	state::units.clear();
	state::twitches.clear();

	Json::Value dwarves = data["dwarves"];
	names = dwarves.getMemberNames();
	for (auto iter = names.begin(); iter != names.end(); iter++)
	{
		int unit_id = stoi(*iter);
		int twitch_id = dwarves[*iter].asInt();
		df::unit *unit = df::unit::find(unit_id);

		if (!chat::users.count(twitch_id))
			continue;	// Twitch user not found - skip
		if (!unit)
			continue;	// Unit not found - skip

		state::units[unit_id] = twitch_id;
		if (!Units::isDead(unit) || !state::twitches.count(twitch_id))
			state::twitches[twitch_id] = unit_id;

		if (config::track_name_changes >= 1)
			checkFixName(unit, twitch_id);
	}
	return true;
}

bool sortTwitch (const TwitchInfo *d1, const TwitchInfo *d2)
{
	if (d1->is_online > d2->is_online) return true;
	if (d2->is_online > d1->is_online) return false;

	if (d1->is_mod > d2->is_mod) return true;
	if (d2->is_mod > d1->is_mod) return false;

	if (d1->is_sub > d2->is_sub) return true;
	if (d2->is_sub > d1->is_sub) return false;


	string n1 = UTF2DF(d1->dispname); std::transform(n1.begin(), n1.end(), n1.begin(), ::tolower);
	string n2 = UTF2DF(d2->dispname); std::transform(n2.begin(), n2.end(), n2.begin(), ::tolower);

	if (n1 < n2) return true;
	if (n2 < n1) return false;

	return false;
}

viewscreen_twitchnamest::viewscreen_twitchnamest(df::unit *src)
{
	chat::lock _lock(chat::mutex);

	unit = src;
	cur_sel = NULL;
	choices.clear();
	int i = 0;
	vector<TwitchInfo *> userlist;
	for (auto iter = chat::users.begin(); iter != chat::users.end(); iter++)
		userlist.push_back(&iter->second);
	std::stable_sort(userlist.begin(), userlist.end(), sortTwitch);
	choices.add(ListEntry<TwitchInfo *>("    (nobody)", NULL));
	
	for (auto iter = userlist.begin(); iter != userlist.end(); iter++)
	{
		TwitchInfo *user = *iter;
		string name = "";
		string keywords = "";
		UIColor color = COLOR_UNSELECTED;
		if (state::twitches.count(user->id))
		{
			if (state::twitches[user->id] == unit->id)
			{
				keywords = "current";
				cur_sel = user;
				color = COLOR_LIGHTCYAN;
			}
			else
			{
				df::unit *unit = df::unit::find(state::twitches[user->id]);
				if (unit && !Units::isDead(unit))
				{
					keywords = "assigned";
					color = COLOR_LIGHTRED;
				}
				else
				{
					keywords = "dead";
					color = COLOR_RED;
				}
			}
		}
		else
		{
			keywords = "available";
		}

		if (user->is_mod)
		{
			name += "\x0F ";
			keywords += ",moderator";
		}
		else	name += "  ";

		if (user->is_sub)
		{
			name += "\x01 ";
			keywords += ",subscriber";
		}
		else	name += "  ";

		name += UTF2DF(user->dispname);

		if (user->is_online)
			keywords += ",online";
		else
		{
			name += " (offline)";
			keywords += ",offline";
		}

		choices.add(ListEntry<TwitchInfo *>(name, user, keywords, color));
		i++;
	}
	choices.filterDisplay();
	if (cur_sel)
	{
		choices.selectItem(cur_sel);
		choices.centerSelection();
	}
	failed_random = false;
}

void viewscreen_twitchnamest::feed(set<df::interface_key> *events)
{
	if (events->count(interface_key::LEAVESCREEN) || !is_enabled)
	{
		Screen::dismiss(this);
		return;
	}
	if (events->count(interface_key::CUSTOM_SHIFT_R))
	{
		auto displist = choices.getDisplayList();
		for (int mode = 0; mode < 4; mode++)
		{
			std::vector<TwitchInfo *> infos;
			for (int i = 0; i < displist.size(); i++)
			{
				TwitchInfo *record = displist[i]->elem;
				// skip "Nobody"
				if (!record)
					continue;
				// skip Offline users
				if (!record->is_online)
					continue;
				// skip Assigned users
				if (state::twitches.count(record->id))
					continue;
				// Filter based on iteration
				switch (mode)
				{
				case 0:
					if (record->is_sub && !record->is_mod)
						infos.push_back(record);
					break;
				case 1:
					if (record->is_sub && record->is_mod)
						infos.push_back(record);
					break;
				case 2:
					if (!record->is_sub && record->is_mod)
						infos.push_back(record);
					break;
				case 3:
					if (!record->is_sub && !record->is_mod)
						infos.push_back(record);
					break;
				}
			}
			if (infos.size())
			{
				std::random_device rd;
				std::minstd_rand gen(rd());
				std::uniform_int_distribution<> dis(0, (int)infos.size() - 1);
				choices.selectItem(infos[dis(gen)]);
				choices.centerSelection();
				failed_random = false;
				return;
			}
		}
		failed_random = true;
	}
	if (!choices.feed(events))
		return;
	if (choices.hasSelection())
	{
		TwitchInfo *new_sel = choices.getFirstSelectedElem();
		if (cur_sel != new_sel)
		{
			if (cur_sel)
				assignUnit(cur_sel, NULL);
			if (new_sel)
				assignUnit(new_sel, unit);
		}
		Screen::dismiss(this);
	}
}

void viewscreen_twitchnamest::render()
{
	auto dim = Screen::getWindowSize();
	int x, y;

	Screen::clear();
	Screen::drawBorder("  Assign Twitch Name  ");

	choices.display(true);

	x = 2; y = 2;
	OutputString(COLOR_WHITE, x, y, "Select a user to Dwarf...");

	x = 2; y = dim.y - 3;
	OutputHotkeyString(x, y, "Select User, ", interface_key::SELECT);
	OutputHotkeyString(x, y, "Choose Random, ", interface_key::CUSTOM_SHIFT_R, false, 0, failed_random ? COLOR_RED : COLOR_GREY);
	OutputHotkeyString(x, y, "Cancel", interface_key::LEAVESCREEN);
}

struct customize_hook : df::viewscreen_customize_unitst
{
	typedef df::viewscreen_customize_unitst interpose_base;

	DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *events))
	{
		if (is_enabled && !editing_nickname && !editing_profession)
		{
			if (events->count(interface_key::CUSTOM_T))
			{
				Screen::show(dts::make_unique<viewscreen_twitchnamest>(unit), plugin_self);
				return;
			}
		}
		INTERPOSE_NEXT(feed)(events);
	}

	DEFINE_VMETHOD_INTERPOSE(void, render, ())
	{
		INTERPOSE_NEXT(render)();
		if (is_enabled)
		{
			int x = 2, y = 4;
			int online = 0;
			{
				chat::lock _lock(chat::mutex);
				for (auto iter = chat::users.begin(); iter != chat::users.end(); iter++)
					online += iter->second.is_online;
			}
			string label = "Assign name from Twitch (" + to_string(online) + "/" + to_string(chat::users.size()) + ")";
			OutputHotkeyString(x, y, label.c_str(), interface_key::CUSTOM_T, false, 0, COLOR_GREY);
		}
	}
};

IMPLEMENT_VMETHOD_INTERPOSE(customize_hook, feed);
IMPLEMENT_VMETHOD_INTERPOSE(customize_hook, render);

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event)
{
	if (!is_enabled)
		return CR_OK;
	string channel = config::channel;

	switch (event)
	{
	case SC_MAP_LOADED:
		if (!loadState(channel))
		{
			if (channel != config::channel)
			{
				out.printerr("twitchname - this fortress is configured for a different channel '%s', please reconfigure\n", channel.c_str());
				is_enabled = false;
				chat::reply = 0;
				chat::command = chat::COMMAND_CLOSE;
				while (!chat::reply)
					_sleep(1);
				chat::nicknames.clear();
				chat::users.clear();
			}
			else	out.printerr("twitchname - failed to load state during fortress startup\n");
		}
		break;
	case SC_MAP_UNLOADED:
		if (!saveState())
			out.printerr("twitchname - failed to save state during fortress shutdown\n");
		break;
	}
	return CR_OK;
}

DFhackCExport command_result plugin_onupdate ( color_ostream &out )
{
	if (is_enabled)
	{
		if (!chat::ready)
		{
			out.printerr("twitchname - error encountered - %s\n", chat::error_msg.c_str());
			if (Core::getInstance().isWorldLoaded() && !saveState())
				out.printerr("twitchname - failed to save state during plugin error\n");
			is_enabled = false;
			chat::nicknames.clear();
			chat::users.clear();
			return CR_OK;
		}
		chat::lock _lock(chat::mutex);
		if (chat::name_changes.size() > 0)
		{
			for (auto iter = state::units.begin(); iter != state::units.end(); iter++)
			{
				int unit_id = iter->first;
				int twitch_id = iter->second;
				if (!chat::name_changes.count(twitch_id))
					continue;
				df::unit *unit = df::unit::find(unit_id);
				if (unit && (config::track_name_changes >= 2))
					checkFixName(unit, twitch_id);
			}
			chat::name_changes.clear();
		}
		while (!chat::debug_log.empty())
		{
			string msg = chat::debug_log.front();
			out.print("twitchname - DEBUG - %s\n", msg.c_str());
			chat::debug_log.pop();
		}
	}
	return CR_OK;
}

command_result df_twitchname (color_ostream &out, vector <string> & parameters)
{
	if (parameters.size() < 1)
		return CR_WRONG_USAGE;

	if (parameters[0] == "disable")
	{
		if (parameters.size() != 1)
			return CR_WRONG_USAGE;

		if (!is_enabled)
		{
			out.print("twitchname - already disabled\n");
			return CR_OK;
		}

		is_enabled = false;
		chat::reply = 0;
		chat::command = chat::COMMAND_CLOSE;
		while (!chat::reply)
			_sleep(1);
		if (Core::getInstance().isWorldLoaded() && !saveState())
			out.printerr("twitchname - failed to save state during plugin disable");

		out.print("twitchname - disabled\n");

		state::units.clear();
		state::twitches.clear();
		chat::nicknames.clear();
		chat::users.clear();
		return CR_OK;
	}
	else if (parameters[0] == "enable")
	{
		if (parameters.size() != 2)
			return CR_WRONG_USAGE;

		if (is_enabled && config::channel == parameters[1])
		{
			out.print("twitchname - already enabled\n");
			return CR_OK;
		}

		// changing channels - disconnect first
		if (is_enabled)
		{
			is_enabled = false;
			chat::reply = 0;
			chat::command = chat::COMMAND_CLOSE;
			while (!chat::reply)
				_sleep(1);
			chat::nicknames.clear();
			chat::users.clear();
		}

		config::channel = parameters[1];
		string channel = config::channel;

		if (Core::getInstance().isWorldLoaded() && !loadState(channel))
		{
			if (channel != config::channel)
			{
				out.printerr("twitchname - current fortress is configured for channel '%s', please reconfigure\n", channel.c_str());
				return CR_FAILURE;
			}
			else	out.printerr("twitchname - failed to load state during plugin enable\n");
		}

		chat::reply = 0;
		chat::command = chat::COMMAND_CONNECT;
		while (!chat::reply)
			_sleep(1);
		if (chat::reply == 1)
		{
			out.print("twitchname - enabled, using channel %s\n", config::channel.c_str());
			is_enabled = true;
			return CR_OK;
		}
		else
		{
			out.print("twitchname - failed to enable: %s\n", chat::error_msg.c_str());
			is_enabled = false;
			return CR_OK;
		}
	}
	else if (parameters[0] == "dump")
	{
		chat::lock _lock(chat::mutex);
		out.print("twitchname - Dumping chat user state\n");
		for (auto iter = chat::users.begin(); iter != chat::users.end(); iter++)
		{
			const auto &user = iter->second;
			out.print("%10i [mod:%i] [sub:%i] [on:%i] %s (%s)\n", user.id, user.is_mod ? 1 : 0, user.is_sub ? 1 : 0, user.is_online ? 1 : 0, user.dispname.c_str(), user.nickname.c_str());
		}
		return CR_OK;
	}
	else if (parameters[0] == "resync")
	{
		bool doFix = true;
		if ((parameters.size() > 1) && (parameters[1] == "check"))
			doFix = false;

		chat::lock _lock(chat::mutex);
		for (auto iter = state::units.begin(); iter != state::units.end(); iter++)
		{
			int unit_id = iter->first;
			int twitch_id = iter->second;
			df::unit *unit = df::unit::find(unit_id);

			if (!chat::users.count(twitch_id))
				continue;	// Twitch user not found - skip
			if (!unit)
				continue;	// Unit not found - skip

			string msg;
			checkFixName(unit, twitch_id, &msg, doFix);
			out.print("twitchname - %s%s\n", msg.c_str(), doFix ? ", applied." : "");
		}
		return CR_OK;
	}
	else if (parameters[0] == "config")
	{
		if (parameters.size() < 3)
		{
			out.printerr("twitchname config - not enough parameters specified\n");
			return CR_WRONG_USAGE;
		}
	        stringstream ss(parameters[2]);
		if (parameters[1] == "track_name_changes")
		{
			int val;
		        ss >> val;
			if ((val >= 0) && (val <= 2))
			{
				out.print("twitchname - '%s' changed to %i\n", parameters[1].c_str(), val);
				config::track_name_changes = val;
				return CR_OK;
			}
			else
			{
				out.printerr("twitchname - invalid value for '%s'\n", parameters[1].c_str());
				return CR_WRONG_USAGE;
			}
		}
		else if (parameters[1] == "announce_name_changes")
		{
			int val;
		        ss >> val;
			if ((val >= 0) && (val <= 1))
			{
				out.print("twitchname - '%s' changed to %i\n", parameters[1].c_str(), val);
				config::announce_name_changes = val;
				return CR_OK;
			}
			else
			{
				out.printerr("twitchname - invalid value for '%s'\n", parameters[1].c_str());
				return CR_WRONG_USAGE;
			}
		}
		else if (parameters[1] == "debug_level")
		{
			int val;
		        ss >> val;
			if ((val >= 0) && (val <= 4))
			{
				out.print("twitchname - '%s' changed to %i\n", parameters[1].c_str(), val);
				config::debug_level = val;
				return CR_OK;
			}
			else
			{
				out.printerr("twitchname - invalid value for '%s'\n", parameters[1].c_str());
				return CR_WRONG_USAGE;
			}
		}
		else
		{
			out.printerr("twitchname config - unknown option '%s'\n", parameters[1].c_str());
			return CR_WRONG_USAGE;
		}
	}
	else
	{
		out.printerr("twitchname - invalid command specified\n");
		return CR_WRONG_USAGE;
	}
}

DFhackCExport command_result plugin_init ( color_ostream &out, vector <PluginCommand> &commands)
{
	if (!INTERPOSE_HOOK(customize_hook, feed).apply(true) ||
		!INTERPOSE_HOOK(customize_hook, render).apply(true))
		return CR_FAILURE;

	commands.push_back(PluginCommand("twitchname", "Configure TwitchName plugin", df_twitchname, false,
		"Usage:\n"
		"\ttwitchname enable <channel> - enables Twitch bot and connects to chat\n"
		"\ttwitchname disable - disconnects from chat\n"
		"\ttwitchname dump - dumps all chat state to the console\n"
		"\ttwitchname resync [check] - immediately update Dwarf names based on Twitch nicknames\n"
		"\t\tcheck - don't rename, just display what names were changed\n"
		"\ttwitchname config <name> <value> - set configuration options\n"
		"Configuration options:\n"
		"\ttrack_name_changes <0-2> - control whether Dwarves get automatically renamed in response to Twitch nickname changes.\n"
		"\t\t0 - do not rename at all\n"
		"\t\t1 - only rename on fortress load\n"
		"\t\t2 - rename in real time\n"
		"\tannounce_name_changes <0-1> - control whether name changes will show up as announcements\n"
		"\t\t0 - don't announce name changes\n"
		"\t\t1 - announce name changes\n"
		"\tdebug_level <0-4> - set debug log level\n"
		"\t\t0 - no events\n"
		"\t\t1 - login events\n"
		"\t\t2 - people joining and leaving chat\n"
		"\t\t3 - people talking in chat\n"
		"\t\t4 - detailed event data\n"
	));

	config::channel = "";
	config::track_name_changes = 0;
	config::announce_name_changes = 0;
	config::debug_level = 0;

	state::twitches.clear();
	state::units.clear();

	chat::nicknames.clear();
	chat::users.clear();
	queue<string>().swap(chat::debug_log);
	chat::name_changes.clear();
	chat::command = chat::COMMAND_NONE;
	chat::thread = new tthread::thread(chat::threadProc, NULL);

	is_enabled = false;
	return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
	INTERPOSE_HOOK(customize_hook, feed).remove();
	INTERPOSE_HOOK(customize_hook, render).remove();

	if (Core::getInstance().isWorldLoaded() && !saveState())
		out.printerr("twitchname - failed to save state during plugin shutdown");

	is_enabled = false;
	chat::command = chat::COMMAND_TERMINATE;
	chat::thread->join();

	chat::nicknames.clear();
	chat::users.clear();
	chat::name_changes.clear();
	queue<string>().swap(chat::debug_log);
	state::twitches.clear();
	state::units.clear();

	return CR_OK;
}
