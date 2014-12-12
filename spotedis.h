#define USER_AGENT "spotedis"
#define REDIS_LIST "spotedis"
#define CACHE_LOCATION "cache"
#define URI_MAX_SIZE 512
#define log printf

void dbg();
void spotify_debug();
void spotify();
static void grab_track();
static void play_track(const char *uri);
static void player_load(sp_session *session);
static void reset_md();
static void logged_in(sp_session *session, sp_error error);
static void logged_out(sp_session *session);
static void metadata_updated(sp_session *session);
static void connection_error(sp_session *session, sp_error error);
static void message_to_user(sp_session *session, const char *message);
void notify_main_thread(sp_session *session);
static int music_delivery(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames);
static void play_token_lost(sp_session *session);
static void log_message(sp_session *session, const char *data);
static void end_of_track(sp_session *session);
static void streaming_error(sp_session *session, sp_error error);
static void userinfo_updated(sp_session *session);
static void start_playback(sp_session *session);
static void stop_playback(sp_session *session);
static void get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats);
static void offline_status_updated(sp_session *session);
static void offline_error(sp_session *session, sp_error error);
static void credentials_blob_updated(sp_session *session, const char *blob);
int spotify_init(const char *username, const char *password);
int spotify_main();

static sp_session_callbacks callbacks = {
    &logged_in,
    &logged_out,
    &metadata_updated,
    &connection_error,
    &message_to_user,
    &notify_main_thread,
    &music_delivery,
    &play_token_lost,
    &log_message,
    &end_of_track,
    &streaming_error,
    &userinfo_updated,
    &start_playback,
    &stop_playback,
    &get_audio_buffer_stats,
    &offline_status_updated,
    &offline_error,
    &credentials_blob_updated
};

const char *g_blob = NULL;
sp_session *g_session;
sp_link *g_link;
redisContext *g_context;
redisReply *g_reply;

static char g_uri[URI_MAX_SIZE];
static bool g_uri_set = 0;
static bool notify_player_load = 0;
static bool is_playing = 0;
static bool md_flag = 0;
static bool md_sent_format = 0;
static int g_sspl = -1;
static int notify_events;
static pthread_mutex_t notify_mutex;
static pthread_cond_t notify_cond;
static int notify_logout = 0;