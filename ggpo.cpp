#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(disable: 4996)
#pragma warning(disable: 4828)
#endif

#include "ggpo.h"
#include "ggponet.h"
#include <scene\main\node.h>
#include <core\engine.h>
#include <core\func_ref.h>
#include <core\reference.h>
#include <list> 


#define MAX_PLAYERS 2
#define MAX_PREDICTION_FRAMES 8
#define MAX_SPECTATORS 8

#define INVALID_HANDLE (-1)

GGPO* GGPO::singleton = NULL;
GGPOSession *ggpo = NULL;

// godot callbacks
Ref<FuncRef> advance_frame_godot_callback;
Ref<FuncRef> load_game_state_godot_callback;
Ref<FuncRef> save_game_state_godot_callback;

GGPO::GGPO() {
	singleton = this;
}

GGPO::~GGPO() {
	singleton = NULL;
}

GGPO *GGPO::get_singleton() {
	return GGPO::singleton;
}

int fletcher32_checksum(short *data, size_t len)
{
   int sum1 = 0xffff, sum2 = 0xffff;

   while (len) {
      size_t tlen = len > 360 ? 360 : len;
      len -= tlen;
      do {
         sum1 += *data++;
         sum2 += sum1;
      } while (--tlen);
      sum1 = (sum1 & 0xffff) + (sum1 >> 16);
      sum2 = (sum2 & 0xffff) + (sum2 >> 16);
   }

   /* Second reduction step to reduce sums to 16 bits */
   sum1 = (sum1 & 0xffff) + (sum1 >> 16);
   sum2 = (sum2 & 0xffff) + (sum2 >> 16);
   return sum2 << 16 | sum1;
}

bool begin_game_callback(const char* game) {
    return true;
}

bool on_event_callback(GGPOEvent *info) {
    int progress;
    switch(info->code) {
        case GGPO_EVENTCODE_CONNECTED_TO_PEER: {
            GGPO::get_singleton()->emit_signal("event_connected_to_peer", info->u.connected.player);
            break;
        }
        case GGPO::EVENTCODE_SYNCHRONIZING_WITH_PEER: {
            progress = 100 * info->u.synchronizing.count / info->u.synchronizing.total;
            GGPO::get_singleton()->emit_signal("event_synchronizing_with_peer", info->u.synchronizing.player, progress);
            break;
        }
        case GGPO::EVENTCODE_SYNCHRONIZED_WITH_PEER: {
            GGPO::get_singleton()->emit_signal("event_synchronized_with_peer", info->u.synchronized.player);
            break;
        }
        case GGPO::EVENTCODE_RUNNING: {
            GGPO::get_singleton()->emit_signal("event_running");
            break;
        }
        case GGPO::EVENTCODE_DISCONNECTED_FROM_PEER: {
            GGPO::get_singleton()->emit_signal("event_disconnected_from_peer", info->u.disconnected.player);
            break;
        }
        case GGPO::EVENTCODE_TIMESYNC: {
            GGPO::get_singleton()->emit_signal("event_timesync", info->u.timesync.frames_ahead);
            break;
        }
        case GGPO::EVENTCODE_CONNECTION_INTERRUPTED: {
            GGPO::get_singleton()->emit_signal("event_connection_interrupted", info->u.connection_interrupted.player, info->u.connection_interrupted.disconnect_timeout);
            break;
        }
        case GGPO::EVENTCODE_CONNECTION_RESUMED: {
            GGPO::get_singleton()->emit_signal("event_connection_resumed", info->u.connection_resumed.player);
            break;
        }
    }

    return true;
}

bool advance_frame_callback(int) {
    int inputs[MAX_PLAYERS] = { 0 };
    int disconnect_flags;

    ggpo_synchronize_input(ggpo, (void *)inputs, sizeof(int) * MAX_PLAYERS, &disconnect_flags);
    Array godot_inputs;
	for (int i = 0; i < MAX_PLAYERS; i++) {
		godot_inputs.append(inputs[i]);
	}
    Variant v_inputs(godot_inputs);
    Variant v_disconnect_flags(disconnect_flags);
    const Variant *args[2] = {&v_inputs, &v_disconnect_flags};
    Variant::CallError err;
    advance_frame_godot_callback->call_func((const Variant **)args, 2, err);
    return true;
}

bool load_game_state_callback(unsigned char *buffer, int len) {
    PoolByteArray game_state;
    game_state.resize(len);
    PoolByteArray::Write write = game_state.write();
    memcpy(write.ptr(), buffer, len);
    GGPO::get_singleton()->emit_signal("load_game_state", game_state);
    return true;
}

bool log_game_state_callback(char *filename, unsigned char *buffer, int len) {
    Variant game_state;
    memcpy(&game_state, buffer, len);
    GGPO::get_singleton()->emit_signal("log_game_state", filename, game_state);
    return true;
}

bool save_game_state_callback(unsigned char **buffer, int *len, int *checksum, int frame) {
    const Variant **args = NULL;
    Variant::CallError err;
    Variant game_state = save_game_state_godot_callback->call_func(args, 0, err);
    PoolByteArray game_state_buffer(game_state);
    *len = game_state_buffer.size();
    *buffer =(unsigned char *)malloc(*len);
    if (!*buffer) {
        return false;
    }
    memcpy(*buffer, game_state_buffer.read().ptr(), *len);
    *checksum = fletcher32_checksum((short *)*buffer, *len / 2);
    return true;
}

void free_buffer_callback(void *buffer) {
    free(buffer);
}

void GGPO::set_advance_frame_callback(Ref<FuncRef> f) {
    advance_frame_godot_callback = f;
}

void GGPO::set_load_game_state_callback(Ref<FuncRef> f) {
    load_game_state_godot_callback = f;
}

void GGPO::set_save_game_state_callback(Ref<FuncRef> f) {
    save_game_state_godot_callback = f;
}

// Used to begin a new GGPO.net session. The ggpo object returned by ggpo_start_session uniquely identifies the state for this session and should be passed to all other functions.
int GGPO::start_session(const String& game, int num_players, int local_port) {
    GGPOSessionCallbacks cb;
    cb.begin_game = &begin_game_callback;
    cb.advance_frame = &advance_frame_callback;
    cb.load_game_state = &load_game_state_callback;
    cb.log_game_state = &log_game_state_callback;
    cb.save_game_state = &save_game_state_callback;
    cb.free_buffer = &free_buffer_callback;
    cb.on_event = &on_event_callback;

    return ggpo_start_session(&ggpo, &cb, game.utf8().get_data(), num_players, sizeof(uint64_t), local_port);
}

// Used to begin a new GGPO.net sync test session. During a sync test, every frame of execution is run twice: once in prediction mode and once again to verify the result of the prediction. If the checksums of your save states do not match, the test is aborted.
int GGPO::start_synctest(const String& game, int num_players, int frames) {
    GGPOSessionCallbacks cb;
    cb.begin_game = &begin_game_callback;
    cb.advance_frame = &advance_frame_callback;
    cb.load_game_state = &load_game_state_callback;
    cb.log_game_state = &log_game_state_callback;
    cb.save_game_state = &save_game_state_callback;
    cb.free_buffer = &free_buffer_callback;
    cb.on_event = &on_event_callback;

    char game_copy[256];
    strcpy(game_copy, game.utf8().get_data());
    return ggpo_start_synctest(&ggpo, &cb, game_copy, num_players, sizeof(uint64_t), frames);
}

// Starts a spectator session
int GGPO::start_spectating(const String& game, int num_players, int local_port, const String& host_ip, int host_port) {
    GGPOSessionCallbacks cb;
    cb.begin_game = &begin_game_callback;
    cb.advance_frame = &advance_frame_callback;
    cb.load_game_state = &load_game_state_callback;
    cb.log_game_state = &log_game_state_callback;
    cb.save_game_state = &save_game_state_callback;
    cb.free_buffer = &free_buffer_callback;
    cb.on_event = &on_event_callback;

    char game_copy[256];
    strcpy(game_copy, game.utf8().get_data());
    char host_ip_copy[256];
    strcpy(host_ip_copy, host_ip.utf8().get_data());
    return ggpo_start_spectating(&ggpo, &cb, game_copy, num_players, sizeof(uint64_t), local_port, host_ip_copy, host_port);
}

// The time to wait before the first GGPO_EVENTCODE_CONNECTION_INTERRUPTED timeout will be sent.
int GGPO::set_disconnect_notify_start(int timeout) {
    return ggpo_set_disconnect_notify_start(ggpo, timeout);
}

// Sets the disconnect timeout. The session will automatically disconnect from a remote peer if it has not received a packet in the timeout window. You will be notified of the disconnect via a GGPO_EVENTCODE_DISCONNECTED_FROM_PEER event. NOTE: Setting a timeout value of 0 will disable automatic disconnects.
int GGPO::set_disconnect_timeout(int timeout) {
    return ggpo_set_disconnect_timeout(ggpo, timeout);
}

// You should call ggpo_synchronize_input before every frame of execution, including those frames which happen during rollback.
Dictionary GGPO::synchronize_input(int length) {
    int inputs[MAX_PLAYERS] = { 0 };
    int disconnect_flags = 0;
    Dictionary ret;

    auto result = ggpo_synchronize_input(ggpo, (void *)inputs, sizeof(int) * MAX_PLAYERS, &disconnect_flags);
    ret["result"] = result;
    if (result == GGPO_ERRORCODE_SUCCESS) {
        Array godot_inputs;
	    for (int i = 0; i < MAX_PLAYERS; i++) {
	    	godot_inputs.append(inputs[i]);
	    }
        ret["inputs"] = godot_inputs;
        ret["disconnect_flags"] = disconnect_flags;
    }

    return ret;
}
// Used to notify GGPO.net of inputs that should be transmitted to remote players. ggpo_add_local_input must be called once every frame for all player of type GGPO_PLAYERTYPE_LOCAL.
int GGPO::add_local_input(int player_handle, int input) {
    int _input = input;
    return ggpo_add_local_input(ggpo, player_handle, &_input, sizeof(input));
}

// Used to close a session. You must call ggpo_close_session to free the resources allocated in ggpo_start_session.
int GGPO::close_session() {
    return ggpo_close_session(ggpo);
}

// Should be called periodically by your application to give GGPO.net a chance to do some work. Most packet transmissions and rollbacks occur in ggpo_idle.
int GGPO::idle(int timeout) {
    return ggpo_idle(ggpo, timeout);
}

// Must be called for each player in the session (e.g. in a 3 player session, must be called 3 times).
Dictionary GGPO::add_player(int player_type, int player_num, const String& ip_address, int port) {
    GGPOPlayer player;
    int player_handle;
    Dictionary ret;

    player.size = sizeof(GGPOPlayer);
    player.type = (GGPOPlayerType)player_type;
    player.player_num = player_num;
    strcpy(player.u.remote.ip_address, ip_address.utf8().get_data());
    player.u.remote.port = port;

    auto result = ggpo_add_player(ggpo, &player, &player_handle);
    ret["result"] = result;
    if (result == GGPO_ERRORCODE_SUCCESS) {
        ret["player_handle"] = player_handle;
        ret["type"] = player.type;
        ret["player_num"] = player.player_num;
        ret["ip_address"] = player.u.remote.ip_address;
        ret["port"] = player.u.remote.port;
    }

    return ret;
}

// Disconnects a remote player from a game. Will return GGPO_ERRORCODE_PLAYER_DISCONNECTED if you try to disconnect a player who has already been disconnected.
int GGPO::disconnect_player(int player_handle) {
    return ggpo_disconnect_player(ggpo, player_handle);
}

// Change the amount of frames ggpo will delay local input. Must be called before the first call to ggpo_synchronize_input.
int GGPO::set_frame_delay(int player_handle, int frame_delay) {
    return ggpo_set_frame_delay(ggpo, player_handle, frame_delay);
}

// You should call ggpo_advance_frame to notify GGPO.net that you have advanced your gamestate by a single frame. You should call this everytime you advance the gamestate by a frame, event during rollbacks. GGPO.net may call your save_game_state callback before this function returns.
int GGPO::advance_frame() {
    return ggpo_advance_frame(ggpo);
}

// Used to write to the ggpo.net log.
void GGPO::log(const String& text) {
    ggpo_log(ggpo, text.utf8().get_data());
}

// Used to fetch some statistics about the quality of the network connection.
Dictionary GGPO::get_network_stats(int player_handle) {
    GGPONetworkStats network_stats;
    Dictionary ret;

    auto result = ggpo_get_network_stats(ggpo, player_handle, &network_stats);
    ret["result"] = result;
    if (result == GGPO_ERRORCODE_SUCCESS) {
        ret["sendQueueLen"] = network_stats.network.send_queue_len;
        ret["recvQueueLen"] = network_stats.network.send_queue_len;
        ret["ping"] = network_stats.network.ping;
        ret["kbpsSent"] = network_stats.network.kbps_sent;
        ret["localFramesBehind"] = network_stats.timesync.local_frames_behind;
        ret["remoteFramesBehind"] = network_stats.timesync.remote_frames_behind;
    }

    return ret;
}

void GGPO::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_advance_frame_callback", "f"), &GGPO::set_advance_frame_callback);
	ClassDB::bind_method(D_METHOD("set_load_game_state_callback", "f"), &GGPO::set_load_game_state_callback);
	ClassDB::bind_method(D_METHOD("set_save_game_state_callback", "f"), &GGPO::set_save_game_state_callback);
    ClassDB::bind_method(D_METHOD("start_session", "game", "num_players", "local_port"), &GGPO::start_session);
    ClassDB::bind_method(D_METHOD("start_synctest", "game", "num_players", "frames"), &GGPO::start_synctest);
    ClassDB::bind_method(D_METHOD("start_spectating", "game", "num_players", "local_port", "host_ip", "host_port"), &GGPO::start_spectating);
    ClassDB::bind_method(D_METHOD("set_disconnect_notify_start", "timeout"), &GGPO::set_disconnect_notify_start);
    ClassDB::bind_method(D_METHOD("set_disconnect_timeout", "timeout"), &GGPO::set_disconnect_timeout);
    ClassDB::bind_method(D_METHOD("synchronize_input", "length"), &GGPO::synchronize_input);
    ClassDB::bind_method(D_METHOD("add_local_input", "player_handle", "input"), &GGPO::add_local_input);
    ClassDB::bind_method(D_METHOD("close_session"), &GGPO::close_session);
    ClassDB::bind_method(D_METHOD("idle", "timeout"), &GGPO::idle);
    ClassDB::bind_method(D_METHOD("add_player", "player_type", "player_num", "ip_address", "port"), &GGPO::add_player, DEFVAL(""), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("disconnect_player", "player_handle"), &GGPO::disconnect_player);
    ClassDB::bind_method(D_METHOD("set_frame_delay", "player_handle", "frame_delay"), &GGPO::set_frame_delay);
    ClassDB::bind_method(D_METHOD("advance_frame"), &GGPO::advance_frame);
    ClassDB::bind_method(D_METHOD("log", "text"), &GGPO::log);
    ClassDB::bind_method(D_METHOD("get_network_stats", "player_handle"), &GGPO::get_network_stats);

    ADD_SIGNAL(MethodInfo("load_game_state", PropertyInfo(Variant::POOL_BYTE_ARRAY, "buffer")));
    ADD_SIGNAL(MethodInfo("log_game_state", PropertyInfo(Variant::STRING, "filename"), PropertyInfo(Variant::POOL_BYTE_ARRAY, "buffer")));
    ADD_SIGNAL(MethodInfo("event_connected_to_peer", PropertyInfo(Variant::INT, "player")));
    ADD_SIGNAL(MethodInfo("event_synchronizing_with_peer", PropertyInfo(Variant::INT, "player"), PropertyInfo(Variant::INT, "progress")));
    ADD_SIGNAL(MethodInfo("event_synchronized_with_peer", PropertyInfo(Variant::INT, "player")));
    ADD_SIGNAL(MethodInfo("event_running"));
    ADD_SIGNAL(MethodInfo("event_disconnected_from_peer", PropertyInfo(Variant::INT, "player")));
    ADD_SIGNAL(MethodInfo("event_timesync", PropertyInfo(Variant::INT, "frames_ahead")));
    ADD_SIGNAL(MethodInfo("event_connection_interrupted", PropertyInfo(Variant::INT, "player"), PropertyInfo(Variant::INT, "disconnect_timeout")));
    ADD_SIGNAL(MethodInfo("event_connection_resumed", PropertyInfo(Variant::INT, "player")));

    BIND_CONSTANT(PLAYERTYPE_LOCAL);
    BIND_CONSTANT(PLAYERTYPE_REMOTE);
    BIND_CONSTANT(PLAYERTYPE_SPECTATOR);
    BIND_CONSTANT(MAX_PLAYERS);
    BIND_CONSTANT(MAX_PREDICTION_FRAMES);
    BIND_CONSTANT(MAX_SPECTATORS);
    BIND_CONSTANT(INVALID_HANDLE);
    BIND_CONSTANT(ERRORCODE_SUCCESS);
    BIND_CONSTANT(ERRORCODE_GENERAL_FAILURE);
    BIND_CONSTANT(ERRORCODE_INVALID_SESSION);
    BIND_CONSTANT(ERRORCODE_INVALID_PLAYER_HANDLE);
    BIND_CONSTANT(ERRORCODE_PLAYER_OUT_OF_RANGE);
    BIND_CONSTANT(ERRORCODE_PREDICTION_THRESHOLD);
    BIND_CONSTANT(ERRORCODE_UNSUPPORTED);
    BIND_CONSTANT(ERRORCODE_NOT_SYNCHRONIZED);
    BIND_CONSTANT(ERRORCODE_IN_ROLLBACK);
    BIND_CONSTANT(ERRORCODE_INPUT_DROPPED);
    BIND_CONSTANT(ERRORCODE_PLAYER_DISCONNECTED);
    BIND_CONSTANT(ERRORCODE_TOO_MANY_SPECTATORS);
    BIND_CONSTANT(ERRORCODE_INVALID_REQUEST);
    BIND_CONSTANT(EVENTCODE_CONNECTED_TO_PEER);
    BIND_CONSTANT(EVENTCODE_SYNCHRONIZING_WITH_PEER);
    BIND_CONSTANT(EVENTCODE_SYNCHRONIZED_WITH_PEER);
    BIND_CONSTANT(EVENTCODE_RUNNING);
    BIND_CONSTANT(EVENTCODE_DISCONNECTED_FROM_PEER);
    BIND_CONSTANT(EVENTCODE_TIMESYNC);
    BIND_CONSTANT(EVENTCODE_CONNECTION_INTERRUPTED);
    BIND_CONSTANT(EVENTCODE_CONNECTION_RESUMED);
}
