#define USER_AGENT "spotedis"
#define log printf

void dbg();
void spotify_debug();
void spotify();
static void connection_error(sp_session *session, sp_error error);
static void logged_in(sp_session *session, sp_error error);
static void logged_out(sp_session *session);
static void log_message(sp_session *session, const char *data);
void notify_main_thread(sp_session *session);
int spotify_init(const char *username, const char *password);
int spotify_main();

static sp_session_callbacks callbacks = {
    &logged_in,
    &logged_out,
    NULL,
    &connection_error,
    NULL,
    &notify_main_thread,
    NULL,
    NULL,
    &log_message
};

const char *g_blob = NULL;

sp_session *g_session;
static int notify_events;
static pthread_mutex_t notify_mutex;
static pthread_cond_t notify_cond;
static int notify_logout = 0;