#ifndef GGPO_H
#define GGPO_H

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(disable: 4996)
#pragma warning(disable: 4828)
#endif

#include <inttypes.h>

#include "ggponet.h"

#include "core/object.h"
#include "core/func_ref.h"
#include "core/reference.h"
#include "core/dictionary.h"
#include "core/engine.h"

namespace Callbacks {
    static bool begin_game(const char* game);
    static bool advance_frame(int flags);
    static bool load_game_state(unsigned char* buffer, int len);
    static bool log_game_state(char* filename, unsigned char* buffer, int len);
    static bool save_game_state(unsigned char** buffer, int* len, int* checksum, int frame);
    static void free_buffer(void* buffer);
    static bool on_event(GGPOEvent* info);
}

class GGPO: public Object {
    GDCLASS(GGPO, Object)

public:
    enum PlayerType {
        PLAYERTYPE_LOCAL, PLAYERTYPE_REMOTE, PLAYERTYPE_SPECTATOR
    };
    enum ErrorCode {
        ERRORCODE_GENERAL_FAILURE = -1, ERRORCODE_SUCCESS = 0, ERRORCODE_INVALID_SESSION = 1, ERRORCODE_INVALID_PLAYER_HANDLE = 2, ERRORCODE_PLAYER_OUT_OF_RANGE = 3, ERRORCODE_PREDICTION_THRESHOLD = 4, ERRORCODE_UNSUPPORTED = 5, ERRORCODE_NOT_SYNCHRONIZED = 6, ERRORCODE_IN_ROLLBACK = 7, ERRORCODE_INPUT_DROPPED = 8, ERRORCODE_PLAYER_DISCONNECTED = 9, ERRORCODE_TOO_MANY_SPECTATORS = 10, ERRORCODE_INVALID_REQUEST = 11
    };
    enum EventCode {
        EVENTCODE_CONNECTED_TO_PEER = 1000, EVENTCODE_SYNCHRONIZING_WITH_PEER = 1001, EVENTCODE_SYNCHRONIZED_WITH_PEER = 1002, EVENTCODE_RUNNING = 1003, EVENTCODE_DISCONNECTED_FROM_PEER = 1004, EVENTCODE_TIMESYNC = 1005, EVENTCODE_CONNECTION_INTERRUPTED = 1006, EVENTCODE_CONNECTION_RESUMED = 1007
    };
    static GGPO* get_singleton();
    GGPO();
    ~GGPO();

    void set_advance_frame_callback(Ref<FuncRef> f);
    void set_load_game_state_callback(Ref<FuncRef> f);
    void set_save_game_state_callback(Ref<FuncRef> f);
    int start_session(const String& game, int num_players, int local_port);
    int start_synctest(const String& game, int num_players, int frames);
    int start_spectating(const String& game, int num_players, int local_port, const String& host_ip, int host_port);
    int set_disconnect_notify_start(int timeout);
    int set_disconnect_timeout(int timeout);
    Dictionary synchronize_input(int length);
    int add_local_input(int player_handle, int input);
    int close_session();
    int idle(int timeout);
    Dictionary add_player(int player_type, int player_num, const String& ip_address, int port);
    int disconnect_player(int player_handle);
    int set_frame_delay(int player_handle, int frame_delay);
    int advance_frame();
    void log(const String& text);
    Dictionary get_network_stats(int player_handle);

protected:
    static void _bind_methods();
    static GGPO* singleton;

};

#endif
