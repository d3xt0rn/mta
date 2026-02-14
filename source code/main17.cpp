// mta_v2.cpp
// Stable terminal ASCII/ANSI video player with optional audio (-A) and sound effects (-S)
// Build: g++ -O2 -std=c++17 -pthread -o mta mta_v2.cpp
// Requires: ffmpeg, ffplay installed and in PATH

#include <bits/stdc++.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <cmath>

using namespace std;

volatile sig_atomic_t g_stop = 0;
void onint(int){ g_stop = 1; }

// Predefined character sets
const string CHARS_DOT = " .";  // -Rp: just dots
const string CHARS_LIGHT = " .:-=+*";  // -Rl: light symbols
const string CHARS_MEDIUM = " .:-=+*#%@&?/\\|()[]{}<>";  // -Rm: medium symbols
const string CHARS_HEAVY = " .:-=+*#%@&?/\\|()[]{}<>0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";  // -Rh: ~105 chars
const string CHARS_ULTRA = " .:-=+*#%@&?/\\|()[]{}<>0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!\"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~¡¢£¤¥¦§¨©ª«¬®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ";  // -Ru: 256 chars

// Preset resolutions (width, height) with target PPI suggestions
struct Preset {
    int width;
    int height;
    string chars;
    string name;
    int target_ppi;  // Suggested characters per inch (for font size hint)
};

// Standard presets
const Preset PRESET_DOT = {40, 24, CHARS_DOT, "Dot (40x24)", 10};
const Preset PRESET_LIGHT = {64, 36, CHARS_LIGHT, "Light (64x36)", 15};
const Preset PRESET_MEDIUM = {96, 54, CHARS_MEDIUM, "Medium (96x54)", 20};
const Preset PRESET_HEAVY = {128, 72, CHARS_HEAVY, "Heavy (128x72)", 25};
const Preset PRESET_ULTRA = {192, 108, CHARS_ULTRA, "Ultra (192x108)", 30};

// Low resolution presets
const Preset PRESET_HORIZONTAL_144 = {256, 144, CHARS_LIGHT, "144p (256x144)", 12};      // -rh144
const Preset PRESET_HORIZONTAL_360 = {640, 360, CHARS_MEDIUM, "360p (640x360)", 18};     // -rh360
const Preset PRESET_HORIZONTAL_480 = {854, 480, CHARS_MEDIUM, "480p (854x480)", 22};     // -rh480

// Vertical low resolution presets (9:16 aspect ratio)
const Preset PRESET_VERTICAL_144 = {144, 256, CHARS_LIGHT, "Vertical 144p (144x256)", 10};    // -rv144
const Preset PRESET_VERTICAL_360 = {360, 640, CHARS_MEDIUM, "Vertical 360p (360x640)", 16};   // -rv360
const Preset PRESET_VERTICAL_480 = {408, 726, CHARS_MEDIUM, "Vertical 480p (408x726)", 20};   // -rv480

// HD presets (16:9 and others)
const Preset PRESET_HD_READY = {1280, 720, CHARS_ULTRA, "HD Ready (1280x720)", 80};
const Preset PRESET_FULL_HD = {1920, 1080, CHARS_ULTRA, "Full HD (1920x1080)", 120};
const Preset PRESET_2K = {2048, 1080, CHARS_ULTRA, "2K (2048x1080)", 130};
const Preset PRESET_WXGA = {1280, 800, CHARS_ULTRA, "WXGA (1280x800)", 85};
const Preset PRESET_WSXGA = {1680, 1050, CHARS_ULTRA, "WSXGA+ (1680x1050)", 100};
const Preset PRESET_UXGA = {1600, 1200, CHARS_ULTRA, "UXGA (1600x1200)", 105};
const Preset PRESET_QHD = {2560, 1440, CHARS_ULTRA, "QHD (2560x1440)", 150};
const Preset PRESET_4K = {3840, 2160, CHARS_ULTRA, "4K UHD (3840x2160)", 200};

// Vertical presets (9:16 aspect ratio) - from lowest to highest
const Preset PRESET_VERTICAL_SD = {360, 640, CHARS_MEDIUM, "Vertical SD (360x640)", 25};        // -Rvsd
const Preset PRESET_VERTICAL_540 = {456, 810, CHARS_HEAVY, "Vertical 540p (456x810)", 31};     // -Rv540 (intermediate 2)
const Preset PRESET_VERTICAL_600 = {504, 896, CHARS_HEAVY, "Vertical 600p (504x896)", 34};     // -Rv600 (intermediate 3)
const Preset PRESET_VERTICAL_660 = {552, 982, CHARS_HEAVY, "Vertical 660p (552x982)", 37};     // -Rv660 (intermediate 4)
const Preset PRESET_VERTICAL_720 = {600, 1068, CHARS_ULTRA, "Vertical 720p (600x1068)", 40};   // -Rv720 (intermediate 5)
const Preset PRESET_VERTICAL_HD = {540, 960, CHARS_HEAVY, "Vertical HD (540x960)", 35};        // -Rvhd (existing)
const Preset PRESET_VERTICAL_FHD = {720, 1280, CHARS_ULTRA, "Vertical FHD (720x1280)", 45};    // -Rvfhd
const Preset PRESET_VERTICAL_2K = {1080, 1920, CHARS_ULTRA, "Vertical 2K (1080x1920)", 70};    // -Rv2k

struct Config {
    string infile;
    bool color256 = false;
    bool truecolor = false;
    bool play_audio = false;
    bool play_sound = false;  // -S flag for sound effects
    bool maintain_aspect = true;  // Keep aspect ratio, don't stretch
    bool vertical_mode = false;  // -V flag for 9:16 vertical video
    bool force_full_terminal = false;  // -Fc flag to use full terminal size
    string custom_aspect = "";  // -Sc flag for custom aspect ratio (e.g., "16:9", "9:16", "1:1")
    string custom_resolution = ""; // -Cr flag for custom resolution (e.g., "800:600")
    int fps = 25;
    int out_w = 0, out_h = 0;
    string chars = " .:-=+*#%@";
    bool autosize = true;
    string preset_name = "";  // For display purposes
    int target_ppi = 20;  // For font size hints
    bool font_hint = false;  // Whether to show font size hint
    bool loop = false;  // -L flag for loop
    float speed = 1.0f;  // -S flag for speed (0.01 to 100)
};

struct VideoInfo {
    double duration = 0.0;  // in seconds
    int64_t total_frames = 0;
    double fps = 25.0;
};

pair<int,int> get_terminal_size(){
    struct winsize w;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) return {80,24};
    return {(int)w.ws_col, (int)w.ws_row};
}

static struct termios oldt;
void set_raw(){ 
    struct termios newt;
    tcgetattr(STDIN_FILENO, &oldt); 
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}
void restore_term(){ 
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); 
    cout << "\x1b[0m\x1b[?25h" << flush; 
}

// Function to suggest terminal font size based on PPI
string get_font_size_suggestion(int target_ppi, int term_cols, int term_rows) {
    // Rough estimate: typical terminal is about 80 columns at 12pt font on 96 DPI display
    // This is just a heuristic
    double current_ppi = (double)term_cols / 80.0 * 12.0; // Rough estimate
    double suggested_size = 12.0 * (target_ppi / current_ppi);
    
    suggested_size = max(6.0, min(72.0, suggested_size)); // Clamp between 6 and 72
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Suggested font size: ~%.1fpt (current cols: %d, target PPI: %d)", 
             suggested_size, term_cols, target_ppi);
    return string(buf);
}

// Get video duration and info using ffprobe
VideoInfo get_video_info(const string& filename) {
    VideoInfo info;
    
    // Get duration
    stringstream cmd_duration;
    cmd_duration << "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \""
                 << filename << "\" 2>/dev/null";
    
    FILE* pipe_duration = popen(cmd_duration.str().c_str(), "r");
    if (pipe_duration) {
        char buf[32];
        if (fgets(buf, sizeof(buf), pipe_duration)) {
            info.duration = atof(buf);
        }
        pclose(pipe_duration);
    }
    
    // Get FPS
    stringstream cmd_fps;
    cmd_fps << "ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 \""
            << filename << "\" 2>/dev/null";
    
    FILE* pipe_fps = popen(cmd_fps.str().c_str(), "r");
    if (pipe_fps) {
        char buf[32];
        if (fgets(buf, sizeof(buf), pipe_fps)) {
            string fps_str = buf;
            size_t slash_pos = fps_str.find('/');
            if (slash_pos != string::npos) {
                double num = atof(fps_str.substr(0, slash_pos).c_str());
                double den = atof(fps_str.substr(slash_pos + 1).c_str());
                if (den > 0) info.fps = num / den;
            } else {
                info.fps = atof(fps_str.c_str());
            }
        }
        pclose(pipe_fps);
    }
    
    if (info.duration > 0 && info.fps > 0) {
        info.total_frames = (int64_t)(info.duration * info.fps);
    }
    
    return info;
}

// Seek to specific position in video (in seconds)
bool seek_video(FILE*& pipe, const string& cmd_base, double position, int out_w, int out_h, int fps) {
    // Close existing pipe
    if (pipe) pclose(pipe);
    
    // Create new command with seek
    stringstream cmd;
    cmd << cmd_base << " -ss " << position << " ";
    
    // Reopen pipe at new position
    pipe = popen(cmd.str().c_str(), "r");
    return pipe != nullptr;
}

// Calculate seek step based on video duration
double get_seek_step(double duration) {
    if (duration <= 0) return 1.0;
    if (duration <= 60) return 1.0;  // 1 second for short videos
    if (duration <= 300) return 5.0;  // 5 seconds for up to 5 min
    if (duration <= 1800) return 15.0; // 15 seconds for up to 30 min
    if (duration <= 3600) return 30.0; // 30 seconds for up to 1 hour
    if (duration <= 7200) return 60.0; // 1 minute for up to 2 hours
    if (duration <= 14400) return 120.0; // 2 minutes for up to 4 hours
    if (duration <= 43200) return 300.0; // 5 minutes for up to 12 hours
    return 600.0; // 10 minutes for very long videos
}

inline int lum(int r, int g, int b){ 
    return (int)(0.2126 * r + 0.7152 * g + 0.0722 * b); 
}

int clampi(int v, int a, int b){ 
    return v < a ? a : (v > b ? b : v); 
}

string ansi256(int r, int g, int b){
    int ir = r / 51, ig = g / 51, ib = b / 51;
    int code = 16 + 36 * ir + 6 * ig + ib;
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", code);
    return buf;
}

string ansi_true(int r, int g, int b){
    char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
    return buf;
}

void play_beep() {
    cout << "\x07" << flush;  // ASCII bell
}

void play_sound_effect(const string& type) {
    // Simple terminal bell variations
    if(type == "start") {
        for(int i = 0; i < 2; i++) {
            cout << "\x07" << flush;
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    } else if(type == "end") {
        cout << "\x07\x07" << flush;
    } else if(type == "error") {
        for(int i = 0; i < 3; i++) {
            cout << "\x07" << flush;
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    } else if(type == "seek") {
        cout << "\x07" << flush;  // Single beep for seek
    }
}

pair<double, double> parse_aspect_ratio(const string& aspect) {
    size_t colon_pos = aspect.find(':');
    if (colon_pos == string::npos) return {16.0, 9.0}; // Default to 16:9
    
    try {
        double w = stod(aspect.substr(0, colon_pos));
        double h = stod(aspect.substr(colon_pos + 1));
        if (w > 0 && h > 0) return {w, h};
    } catch (...) {
        // Parsing failed, return default
    }
    return {16.0, 9.0};
}

// Parse custom resolution string like "1920:1080" or "800x600"
pair<int, int> parse_resolution(const string& res) {
    size_t colon_pos = res.find(':');
    size_t x_pos = res.find('x');
    size_t delim_pos = (colon_pos != string::npos) ? colon_pos : x_pos;
    
    if (delim_pos == string::npos) return {0, 0};
    
    try {
        int w = stoi(res.substr(0, delim_pos));
        int h = stoi(res.substr(delim_pos + 1));
        if (w > 0 && h > 0) {
            // Ensure even height (for 2 lines per row)
            if (h % 2) h--;
            return {w, h};
        }
    } catch (...) {
        // Parsing failed
    }
    return {0, 0};
}

void usage(){
    cout << "Usage: mta_v2 <video.mp4> [options]\n\n"
         << "Options:\n"
         << "  -C              Enable TrueColor (24-bit)\n"
         << "  -256            Force 256-color mode\n"
         << "  -F<N>           Set FPS (default 25)\n"
         << "  -A              Play audio with ffplay\n"
         << "  -S <speed>      Set playback speed (0.01 to 100, default 1.0)\n"
         << "  -L              Enable loop mode\n"
         << "  -V              Vertical mode (9:16 aspect ratio)\n"
         << "  -Sc <W:H>       Custom aspect ratio (e.g., -Sc 1:1, -Sc 9:16, -Sc 4:3)\n"
         << "  -Cr <W:H>       Custom resolution (e.g., -Cr 800:600, -Cr 1920x1080)\n"
         << "  -Fc             Force full terminal size (stretch to fill entire terminal)\n"
         << "  -font-hint      Show suggested font size for current resolution\n\n"
         << "Resolution Presets (maintain aspect ratio):\n"
         << "  Standard:\n"
         << "    -Rp           Dot preset (40x24)\n"
         << "    -Rl           Light preset (64x36)\n"
         << "    -Rm           Medium preset (96x54)\n"
         << "    -Rh           Heavy preset (128x72)\n"
         << "    -Ru           Ultra preset (192x108)\n\n"
         << "  Low Resolution Presets:\n"
         << "    -rh144        144p horizontal (256x144)\n"
         << "    -rh360        360p horizontal (640x360)\n"
         << "    -rh480        480p horizontal (854x480)\n"
         << "    -rv144        Vertical 144p (144x256)\n"
         << "    -rv360        Vertical 360p (360x640)\n"
         << "    -rv480        Vertical 480p (408x726)\n\n"
         << "  HD Presets (16:9):\n"
         << "    -Rh720        HD Ready (1280x720)\n"
         << "    -Rfhd         Full HD (1920x1080)\n"
         << "    -R2k          2K (2048x1080)\n"
         << "    -Rwxga        WXGA (1280x800) - 16:10\n"
         << "    -Rwsxga       WSXGA+ (1680x1050) - 16:10\n"
         << "    -Ruxga        UXGA (1600x1200) - 4:3\n"
         << "    -Rqhd         QHD (2560x1440)\n"
         << "    -R4k          4K UHD (3840x2160)\n\n"
         << "  Vertical Presets (9:16):\n"
         << "    -Rvsd         Vertical SD (360x640)\n"
         << "    -Rv540        Vertical 540p (456x810)\n"
         << "    -Rv600        Vertical 600p (504x896)\n"
         << "    -Rv660        Vertical 660p (552x982)\n"
         << "    -Rv720        Vertical 720p (600x1068)\n"
         << "    -Rvhd         Vertical HD (540x960)\n"
         << "    -Rvfhd        Vertical FHD (720x1280)\n"
         << "    -Rv2k         Vertical 2K (1080x1920)\n\n"
         << "  -chars \"...\"    Custom ASCII ramp (overrides presets)\n"
         << "  -stretch        Stretch video to fill terminal (default: maintain aspect ratio)\n"
         << "  -h              Show this help\n\n"
         << "Playback Controls:\n"
         << "  Space           Pause/Resume\n"
         << "  Left/Right      Seek backward/forward (step depends on video length)\n"
         << "  w/s             Increase/Decrease playback speed\n"
         << "  L               Toggle loop mode\n"
         << "  b               Manual beep (if -S enabled)\n"
         << "  q/Esc           Quit\n\n"
         << "Examples:\n"
         << "  ./mta_v2 video.mp4 -256 -F30 -A -S 1.5 -L -Rfhd\n"
         << "  ./mta_v2 video.mp4 -V -Rvfhd -S 0.5        # Slow motion vertical\n"
         << "  ./mta_v2 video.mp4 -Sc 1:1 -Rp -L          # Square loop\n"
         << "  ./mta_v2 video.mp4 -R4k -font-hint -S 2.0  # 2x speed 4K\n";
    exit(0);
}

bool ffmpeg_exists(){
    return system("which ffmpeg > /dev/null 2>&1") == 0;
}

bool ffplay_exists(){
    return system("which ffplay > /dev/null 2>&1") == 0;
}

// Get video dimensions without processing
pair<int, int> get_video_dimensions(const string& filename) {
    stringstream cmd;
    cmd << "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 \""
        << filename << "\" 2>/dev/null";
    
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return {0, 0};
    
    int width = 0, height = 0;
    fscanf(pipe, "%d,%d", &width, &height);
    pclose(pipe);
    
    return {width, height};
}

// Calculate output dimensions maintaining aspect ratio
pair<int, int> calculate_dimensions(int video_w, int video_h, int term_cols, int term_rows, const Config& cfg, int base_w = 0, int base_h = 0) {
    if (cfg.force_full_terminal) {
        return {term_cols, term_rows * 2};
    }
    
    if (video_w == 0 || video_h == 0) return {term_cols, term_rows * 2};
    
    double target_aspect = (double)video_w / video_h;
    
    // Apply custom aspect ratio if specified
    if (!cfg.custom_aspect.empty()) {
        auto [aw, ah] = parse_aspect_ratio(cfg.custom_aspect);
        target_aspect = aw / ah;
    } else if (cfg.vertical_mode) {
        target_aspect = 9.0 / 16.0; // 9:16 vertical mode
    }
    
    // If we have a preset base size, use it as maximum dimensions
    if (base_w > 0 && base_h > 0) {
        double base_aspect = (double)base_w / base_h;
        
        // Determine which dimension to use as constraint
        if (target_aspect > base_aspect) {
            // Video is wider than base - fit to width
            int out_w = base_w;
            int out_h = (int)(out_w / target_aspect);
            if (out_h % 2) out_h--;
            // Ensure we don't exceed base height
            if (out_h > base_h) {
                out_h = base_h;
                out_w = (int)(out_h * target_aspect);
            }
            return {out_w, out_h};
        } else {
            // Video is taller than base - fit to height
            int out_h = base_h;
            int out_w = (int)(out_h * target_aspect);
            if (out_w % 2) out_w--; // Ensure even width (though not strictly necessary)
            // Ensure we don't exceed base width
            if (out_w > base_w) {
                out_w = base_w;
                out_h = (int)(out_w / target_aspect);
            }
            return {out_w, out_h};
        }
    }
    
    // Terminal rows to video height ratio (each terminal row shows 2 video lines)
    double term_aspect = (double)term_cols / (term_rows * 2);
    
    int out_w, out_h;
    
    if (target_aspect > term_aspect) {
        // Video is wider than terminal - fit to width
        out_w = term_cols;
        out_h = (int)(out_w / target_aspect);
        // Ensure height is even (for 2 lines per row)
        if (out_h % 2) out_h--;
    } else {
        // Video is taller than terminal - fit to height
        out_h = term_rows * 2;
        out_w = (int)(out_h * target_aspect);
    }
    
    // Ensure minimum size
    out_w = max(10, out_w);
    out_h = max(10, out_h);
    
    // Ensure even height
    if (out_h % 2) out_h--;
    
    return {out_w, out_h};
}

// Draw progress bar and status
void draw_status_bar(double current_time, double total_time, bool paused, bool loop, float speed, int cols) {
    if (total_time <= 0) return;
    
    // Save cursor position
    cout << "\x1b[s";
    
    // Move to bottom line
    cout << "\x1b[" << (get_terminal_size().second) << ";1H";
    
    // Clear line
    cout << "\x1b[2K";
    
    // Calculate progress
    double progress = min(1.0, max(0.0, current_time / total_time));
    int bar_width = cols - 30; // Reserve space for time and status
    
    // Format time strings
    int current_h = (int)(current_time / 3600);
    int current_m = (int)(fmod(current_time, 3600) / 60);
    int current_s = (int)fmod(current_time, 60);
    
    int total_h = (int)(total_time / 3600);
    int total_m = (int)(fmod(total_time, 3600) / 60);
    int total_s = (int)fmod(total_time, 60);
    
    char time_buf[32];
    if (total_h > 0) {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d/%02d:%02d:%02d", 
                 current_h, current_m, current_s, total_h, total_m, total_s);
    } else {
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d/%02d:%02d", 
                 current_m, current_s, total_m, total_s);
    }
    
    // Draw progress bar
    cout << "\x1b[37m["; // White color
    
    int pos = (int)(bar_width * progress);
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) cout << "=";
        else if (i == pos) cout << ">";
        else cout << "-";
    }
    
    cout << "] " << time_buf << " ";
    
    // Status indicators
    if (paused) cout << "PAUSED ";
    if (loop) cout << "LOOP ";
    
    cout << "speed: " << fixed << setprecision(2) << speed << "x";
    
    // Reset color and restore cursor
    cout << "\x1b[0m\x1b[u" << flush;
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if(argc < 2) usage();
    signal(SIGINT, onint);
    signal(SIGTERM, onint);

    Config cfg;
    cfg.infile = argv[1];
    
    // First pass: check for preset flags to set base dimensions
    bool has_preset = false;
    bool has_custom_res = false;
    int preset_base_w = 0, preset_base_h = 0;
    
    for(int i = 2; i < argc; i++){
        string s = argv[i];
        
        // Standard presets
        if(s == "-Rh"){
            has_preset = true;
            preset_base_w = PRESET_HEAVY.width;
            preset_base_h = PRESET_HEAVY.height;
            cfg.chars = PRESET_HEAVY.chars;
            cfg.preset_name = PRESET_HEAVY.name;
            cfg.target_ppi = PRESET_HEAVY.target_ppi;
        }
        else if(s == "-Rm"){
            has_preset = true;
            preset_base_w = PRESET_MEDIUM.width;
            preset_base_h = PRESET_MEDIUM.height;
            cfg.chars = PRESET_MEDIUM.chars;
            cfg.preset_name = PRESET_MEDIUM.name;
            cfg.target_ppi = PRESET_MEDIUM.target_ppi;
        }
        else if(s == "-Rl"){
            has_preset = true;
            preset_base_w = PRESET_LIGHT.width;
            preset_base_h = PRESET_LIGHT.height;
            cfg.chars = PRESET_LIGHT.chars;
            cfg.preset_name = PRESET_LIGHT.name;
            cfg.target_ppi = PRESET_LIGHT.target_ppi;
        }
        else if(s == "-Rp"){
            has_preset = true;
            preset_base_w = PRESET_DOT.width;
            preset_base_h = PRESET_DOT.height;
            cfg.chars = PRESET_DOT.chars;
            cfg.preset_name = PRESET_DOT.name;
            cfg.target_ppi = PRESET_DOT.target_ppi;
        }
        else if(s == "-Ru"){
            has_preset = true;
            preset_base_w = PRESET_ULTRA.width;
            preset_base_h = PRESET_ULTRA.height;
            cfg.chars = PRESET_ULTRA.chars;
            cfg.preset_name = PRESET_ULTRA.name;
            cfg.target_ppi = PRESET_ULTRA.target_ppi;
        }
        // New low resolution horizontal presets
        else if(s == "-rh144"){
            has_preset = true;
            preset_base_w = PRESET_HORIZONTAL_144.width;
            preset_base_h = PRESET_HORIZONTAL_144.height;
            cfg.chars = PRESET_HORIZONTAL_144.chars;
            cfg.preset_name = PRESET_HORIZONTAL_144.name;
            cfg.target_ppi = PRESET_HORIZONTAL_144.target_ppi;
        }
        else if(s == "-rh360"){
            has_preset = true;
            preset_base_w = PRESET_HORIZONTAL_360.width;
            preset_base_h = PRESET_HORIZONTAL_360.height;
            cfg.chars = PRESET_HORIZONTAL_360.chars;
            cfg.preset_name = PRESET_HORIZONTAL_360.name;
            cfg.target_ppi = PRESET_HORIZONTAL_360.target_ppi;
        }
        else if(s == "-rh480"){
            has_preset = true;
            preset_base_w = PRESET_HORIZONTAL_480.width;
            preset_base_h = PRESET_HORIZONTAL_480.height;
            cfg.chars = PRESET_HORIZONTAL_480.chars;
            cfg.preset_name = PRESET_HORIZONTAL_480.name;
            cfg.target_ppi = PRESET_HORIZONTAL_480.target_ppi;
        }
        // New low resolution vertical presets
        else if(s == "-rv144"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_144.width;
            preset_base_h = PRESET_VERTICAL_144.height;
            cfg.chars = PRESET_VERTICAL_144.chars;
            cfg.preset_name = PRESET_VERTICAL_144.name;
            cfg.target_ppi = PRESET_VERTICAL_144.target_ppi;
        }
        else if(s == "-rv360"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_360.width;
            preset_base_h = PRESET_VERTICAL_360.height;
            cfg.chars = PRESET_VERTICAL_360.chars;
            cfg.preset_name = PRESET_VERTICAL_360.name;
            cfg.target_ppi = PRESET_VERTICAL_360.target_ppi;
        }
        else if(s == "-rv480"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_480.width;
            preset_base_h = PRESET_VERTICAL_480.height;
            cfg.chars = PRESET_VERTICAL_480.chars;
            cfg.preset_name = PRESET_VERTICAL_480.name;
            cfg.target_ppi = PRESET_VERTICAL_480.target_ppi;
        }
        // HD presets (16:9 and others)
        else if(s == "-Rh720"){
            has_preset = true;
            preset_base_w = PRESET_HD_READY.width;
            preset_base_h = PRESET_HD_READY.height;
            cfg.chars = PRESET_HD_READY.chars;
            cfg.preset_name = PRESET_HD_READY.name;
            cfg.target_ppi = PRESET_HD_READY.target_ppi;
        }
        else if(s == "-Rfhd"){
            has_preset = true;
            preset_base_w = PRESET_FULL_HD.width;
            preset_base_h = PRESET_FULL_HD.height;
            cfg.chars = PRESET_FULL_HD.chars;
            cfg.preset_name = PRESET_FULL_HD.name;
            cfg.target_ppi = PRESET_FULL_HD.target_ppi;
        }
        else if(s == "-R2k"){
            has_preset = true;
            preset_base_w = PRESET_2K.width;
            preset_base_h = PRESET_2K.height;
            cfg.chars = PRESET_2K.chars;
            cfg.preset_name = PRESET_2K.name;
            cfg.target_ppi = PRESET_2K.target_ppi;
        }
        else if(s == "-Rwxga"){
            has_preset = true;
            preset_base_w = PRESET_WXGA.width;
            preset_base_h = PRESET_WXGA.height;
            cfg.chars = PRESET_WXGA.chars;
            cfg.preset_name = PRESET_WXGA.name;
            cfg.target_ppi = PRESET_WXGA.target_ppi;
        }
        else if(s == "-Rwsxga"){
            has_preset = true;
            preset_base_w = PRESET_WSXGA.width;
            preset_base_h = PRESET_WSXGA.height;
            cfg.chars = PRESET_WSXGA.chars;
            cfg.preset_name = PRESET_WSXGA.name;
            cfg.target_ppi = PRESET_WSXGA.target_ppi;
        }
        else if(s == "-Ruxga"){
            has_preset = true;
            preset_base_w = PRESET_UXGA.width;
            preset_base_h = PRESET_UXGA.height;
            cfg.chars = PRESET_UXGA.chars;
            cfg.preset_name = PRESET_UXGA.name;
            cfg.target_ppi = PRESET_UXGA.target_ppi;
        }
        else if(s == "-Rqhd"){
            has_preset = true;
            preset_base_w = PRESET_QHD.width;
            preset_base_h = PRESET_QHD.height;
            cfg.chars = PRESET_QHD.chars;
            cfg.preset_name = PRESET_QHD.name;
            cfg.target_ppi = PRESET_QHD.target_ppi;
        }
        else if(s == "-R4k"){
            has_preset = true;
            preset_base_w = PRESET_4K.width;
            preset_base_h = PRESET_4K.height;
            cfg.chars = PRESET_4K.chars;
            cfg.preset_name = PRESET_4K.name;
            cfg.target_ppi = PRESET_4K.target_ppi;
        }
        // Vertical presets
        else if(s == "-Rvsd"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_SD.width;
            preset_base_h = PRESET_VERTICAL_SD.height;
            cfg.chars = PRESET_VERTICAL_SD.chars;
            cfg.preset_name = PRESET_VERTICAL_SD.name;
            cfg.target_ppi = PRESET_VERTICAL_SD.target_ppi;
        }
        else if(s == "-Rv540"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_540.width;
            preset_base_h = PRESET_VERTICAL_540.height;
            cfg.chars = PRESET_VERTICAL_540.chars;
            cfg.preset_name = PRESET_VERTICAL_540.name;
            cfg.target_ppi = PRESET_VERTICAL_540.target_ppi;
        }
        else if(s == "-Rv600"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_600.width;
            preset_base_h = PRESET_VERTICAL_600.height;
            cfg.chars = PRESET_VERTICAL_600.chars;
            cfg.preset_name = PRESET_VERTICAL_600.name;
            cfg.target_ppi = PRESET_VERTICAL_600.target_ppi;
        }
        else if(s == "-Rv660"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_660.width;
            preset_base_h = PRESET_VERTICAL_660.height;
            cfg.chars = PRESET_VERTICAL_660.chars;
            cfg.preset_name = PRESET_VERTICAL_660.name;
            cfg.target_ppi = PRESET_VERTICAL_660.target_ppi;
        }
        else if(s == "-Rv720"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_720.width;
            preset_base_h = PRESET_VERTICAL_720.height;
            cfg.chars = PRESET_VERTICAL_720.chars;
            cfg.preset_name = PRESET_VERTICAL_720.name;
            cfg.target_ppi = PRESET_VERTICAL_720.target_ppi;
        }
        else if(s == "-Rvhd"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_HD.width;
            preset_base_h = PRESET_VERTICAL_HD.height;
            cfg.chars = PRESET_VERTICAL_HD.chars;
            cfg.preset_name = PRESET_VERTICAL_HD.name;
            cfg.target_ppi = PRESET_VERTICAL_HD.target_ppi;
        }
        else if(s == "-Rvfhd"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_FHD.width;
            preset_base_h = PRESET_VERTICAL_FHD.height;
            cfg.chars = PRESET_VERTICAL_FHD.chars;
            cfg.preset_name = PRESET_VERTICAL_FHD.name;
            cfg.target_ppi = PRESET_VERTICAL_FHD.target_ppi;
        }
        else if(s == "-Rv2k"){
            has_preset = true;
            preset_base_w = PRESET_VERTICAL_2K.width;
            preset_base_h = PRESET_VERTICAL_2K.height;
            cfg.chars = PRESET_VERTICAL_2K.chars;
            cfg.preset_name = PRESET_VERTICAL_2K.name;
            cfg.target_ppi = PRESET_VERTICAL_2K.target_ppi;
        }
    }
    
    // Second pass: process other flags
    for(int i = 2; i < argc; i++){
        string s = argv[i];
        if(s == "-C") cfg.truecolor = true;
        else if(s == "-256") cfg.color256 = true;
        else if(s == "-A") cfg.play_audio = true;
        else if(s == "-L") cfg.loop = true;
        else if(s == "-V") cfg.vertical_mode = true;
        else if(s == "-Fc") cfg.force_full_terminal = true;
        else if(s == "-font-hint") cfg.font_hint = true;
        else if(s == "-stretch") cfg.maintain_aspect = false;
        else if(s == "-S" && i+1 < argc) {
            float speed_val = atof(argv[++i]);
            cfg.speed = max(0.01f, min(100.0f, speed_val));
        }
        else if(s.rfind("-F", 0) == 0){ 
            string n = s.substr(2); 
            if(n.empty() && i+1 < argc) n = argv[++i]; 
            cfg.fps = max(1, stoi(n)); 
        }
        else if(s == "-Sc" && i+1 < argc) {
            cfg.custom_aspect = argv[++i];
            // Validate format
            if (cfg.custom_aspect.find(':') == string::npos) {
                cerr << "Warning: Invalid aspect ratio format. Use W:H (e.g., 16:9, 9:16, 1:1)\n";
                cfg.custom_aspect = "";
            }
        }
        else if(s == "-Cr" && i+1 < argc) {
            cfg.custom_resolution = argv[++i];
            auto [w, h] = parse_resolution(cfg.custom_resolution);
            if (w > 0 && h > 0) {
                has_custom_res = true;
                preset_base_w = w;
                preset_base_h = h;
                cfg.preset_name = "Custom " + to_string(w) + "x" + to_string(h);
                cfg.target_ppi = 20; // Default for custom
                cfg.autosize = false;
            } else {
                cerr << "Warning: Invalid custom resolution format. Use W:H or WxH (e.g., 800:600, 1920x1080)\n";
            }
        }
        else if(s == "-Rh" || s == "-Rm" || s == "-Rl" || s == "-Rp" || s == "-Ru" ||
                s == "-rh144" || s == "-rh360" || s == "-rh480" ||
                s == "-rv144" || s == "-rv360" || s == "-rv480" ||
                s == "-Rh720" || s == "-Rfhd" || s == "-R2k" || s == "-Rwxga" || 
                s == "-Rwsxga" || s == "-Ruxga" || s == "-Rqhd" || s == "-R4k" ||
                s == "-Rvsd" || s == "-Rv540" || s == "-Rv600" || s == "-Rv660" || 
                s == "-Rv720" || s == "-Rvhd" || s == "-Rvfhd" || s == "-Rv2k") {
            // Already handled in first pass
            cfg.autosize = false;
        }
        else if(s == "-chars" && i+1 < argc){ 
            cfg.chars = argv[++i]; 
        }
        else if(s == "-h" || s == "--help") usage();
    }

    if(!ffmpeg_exists()){ 
        cerr << "ffmpeg not found! Install with: sudo pacman -S ffmpeg\n"; 
        if(cfg.play_sound) play_sound_effect("error");
        return 1; 
    }
    
    if(cfg.play_audio && !ffplay_exists()){ 
        cerr << "ffplay not found! Install with: sudo pacman -S ffmpeg\n"; 
        if(cfg.play_sound) play_sound_effect("error");
        return 1; 
    }

    // Get terminal size
    auto [cols, rows] = get_terminal_size();
    
    if(cfg.play_sound) play_sound_effect("start");
    
    // Get video dimensions and info
    auto [video_w, video_h] = get_video_dimensions(cfg.infile);
    VideoInfo video_info = get_video_info(cfg.infile);
    
    // Calculate output dimensions
    if(cfg.autosize) {
        if(cfg.maintain_aspect && video_w > 0 && video_h > 0) {
            tie(cfg.out_w, cfg.out_h) = calculate_dimensions(video_w, video_h, cols, rows, cfg);
        } else {
            cfg.out_w = cols;
            cfg.out_h = rows * 2;
        }
    } else if (has_preset && cfg.maintain_aspect && video_w > 0 && video_h > 0) {
        // For presets, maintain aspect ratio within the preset's maximum dimensions
        tie(cfg.out_w, cfg.out_h) = calculate_dimensions(video_w, video_h, cols, rows, cfg, preset_base_w, preset_base_h);
    } else if (has_preset || has_custom_res) {
        // Use preset/custom dimensions directly if not maintaining aspect
        cfg.out_w = preset_base_w;
        cfg.out_h = preset_base_h;
    }

    // Calculate centering offsets
    int x_offset = 0, y_offset = 0;
    if(!cfg.force_full_terminal && cfg.maintain_aspect && cfg.out_w < cols) {
        x_offset = (cols - cfg.out_w) / 2;
    }
    
    if(!cfg.force_full_terminal && cfg.maintain_aspect && cfg.out_h < rows * 2) {
        y_offset = (rows * 2 - cfg.out_h) / 2;
        // Convert to terminal rows (2 video lines per row)
        y_offset /= 2;
    }

    // Display configuration info
    if(video_w > 0 && video_h > 0) {
        cerr << "Video: " << video_w << "x" << video_h;
        if(!cfg.custom_aspect.empty()) {
            cerr << ", Custom aspect: " << cfg.custom_aspect;
        } else if(cfg.vertical_mode) {
            cerr << ", Vertical mode (9:16)";
        }
        if(!cfg.preset_name.empty()) {
            cerr << "\nPreset: " << cfg.preset_name;
        }
        if(!cfg.custom_resolution.empty()) {
            cerr << "\nCustom resolution: " << cfg.custom_resolution;
        }
        cerr << "\nTerminal: " << cols << "x" << rows;
        cerr << "\nOutput: " << cfg.out_w << "x" << cfg.out_h;
        if(x_offset > 0 || y_offset > 0) {
            cerr << " (centered: " << x_offset << "," << y_offset << ")";
        }
        
        if (video_info.duration > 0) {
            int h = (int)(video_info.duration / 3600);
            int m = (int)(fmod(video_info.duration, 3600) / 60);
            int s = (int)fmod(video_info.duration, 60);
            if (h > 0) {
                cerr << "\nDuration: " << h << "h " << m << "m " << s << "s";
            } else {
                cerr << "\nDuration: " << m << "m " << s << "s";
            }
        }
        
        // Show font size hint if requested
        if(cfg.font_hint && (has_preset || has_custom_res)) {
            cerr << "\n" << get_font_size_suggestion(cfg.target_ppi, cols, rows);
        }
        
        cerr << "\n\nControls: Space=Pause, L=Loop, w/s=Speed, ←/→=Seek, b=Beep, q=Quit\n" << flush;
    }

    // start audio async if requested
    thread audio_thr;
    if(cfg.play_audio){
        string cmd_audio = "ffplay -nodisp -autoexit -loglevel quiet \"" + cfg.infile + "\" &";
        system(cmd_audio.c_str());
    }

    // prepare ffmpeg command base (without seek)
    stringstream cmd_base;
    cmd_base << "ffmpeg -i \"" << cfg.infile << "\" -loglevel quiet -an "
        << "-f rawvideo -pix_fmt rgb24 -r " << cfg.fps;
    
    // If we have custom aspect ratio or preset, we might need to scale the video
    if((!cfg.custom_aspect.empty() || cfg.vertical_mode || has_preset || has_custom_res) && !cfg.force_full_terminal && cfg.maintain_aspect) {
        // Let ffmpeg handle the scaling with the target aspect ratio
        cmd_base << " -vf \"scale=" << cfg.out_w << ":" << cfg.out_h << ":force_original_aspect_ratio=1\"";
    }
    
    cmd_base << " -s " << cfg.out_w << "x" << cfg.out_h << " pipe:1";
    
    string base_cmd_str = cmd_base.str();
    FILE* pipe = popen(base_cmd_str.c_str(), "r");
    if(!pipe){ 
        cerr << "Error: failed to start ffmpeg!\n"; 
        if(cfg.play_sound) play_sound_effect("error");
        return 1; 
    }

    set_raw();
    cout << "\x1b[2J\x1b[?25l" << flush;

    size_t frame_bytes = (size_t)cfg.out_w * cfg.out_h * 3;
    vector<unsigned char> frame(frame_bytes);
    const double base_frame_dt = 1.0 / cfg.fps;
    int ramp_len = cfg.chars.size();

    auto last = chrono::steady_clock::now();
    double current_time = 0.0;
    bool paused = false;
    float current_speed = cfg.speed;
    int64_t frame_count = 0;
    double seek_step = get_seek_step(video_info.duration);
    
    while(!g_stop){
        if (!paused) {
            size_t got = fread(frame.data(), 1, frame_bytes, pipe);
            if(got < frame_bytes) {
                if (cfg.loop) {
                    // Loop video
                    pclose(pipe);
                    pipe = popen(base_cmd_str.c_str(), "r");
                    if (!pipe) break;
                    frame_count = 0;
                    current_time = 0.0;
                    continue;
                } else {
                    break;
                }
            }
            
            frame_count++;
            if (video_info.fps > 0) {
                current_time = frame_count / video_info.fps;
            }
        }

        cout << "\x1b[H";
        
        // Add top padding for vertical centering
        if(y_offset > 0) {
            for(int i = 0; i < y_offset; i++) {
                cout << "\n";
            }
        }
        
        if (!paused) {
            for(int y = 0; y < cfg.out_h; y += 2){ // 2 lines per terminal row
                // Add left padding for centering
                if(x_offset > 0) {
                    cout << string(x_offset, ' ');
                }
                
                for(int x = 0; x < cfg.out_w; x++){
                    size_t idx = (y * cfg.out_w + x) * 3;
                    int r = frame[idx], g = frame[idx+1], b = frame[idx+2];
                    int l = lum(r, g, b);
                    char c = cfg.chars[clampi(l * (ramp_len - 1) / 255, 0, ramp_len - 1)];
                    
                    if(cfg.truecolor) cout << ansi_true(r, g, b) << c;
                    else if(cfg.color256) cout << ansi256(r, g, b) << c;
                    else cout << c;
                }
                cout << "\x1b[0m\n";
            }
        } else {
            // Just redraw last frame when paused
            for(int y = 0; y < cfg.out_h; y += 2){
                if(x_offset > 0) {
                    cout << string(x_offset, ' ');
                }
                
                for(int x = 0; x < cfg.out_w; x++){
                    size_t idx = (y * cfg.out_w + x) * 3;
                    int r = frame[idx], g = frame[idx+1], b = frame[idx+2];
                    int l = lum(r, g, b);
                    char c = cfg.chars[clampi(l * (ramp_len - 1) / 255, 0, ramp_len - 1)];
                    
                    if(cfg.truecolor) cout << ansi_true(r, g, b) << c;
                    else if(cfg.color256) cout << ansi256(r, g, b) << c;
                    else cout << c;
                }
                cout << "\x1b[0m\n";
            }
        }
        
        // Fill remaining lines if needed (for full terminal mode or when video is smaller)
        if(cfg.force_full_terminal || y_offset > 0) {
            int lines_remaining = rows - (cfg.out_h / 2) - y_offset;
            for(int i = 0; i < lines_remaining; i++) {
                cout << "\n";
            }
        }
        
        // Draw status bar
        draw_status_bar(current_time, video_info.duration, paused, cfg.loop, current_speed, cols);
        
        cout.flush();

        // key check
        int c;
        fd_set fds; 
        struct timeval tv = {0, 0};
        FD_ZERO(&fds); 
        FD_SET(STDIN_FILENO, &fds);
        
        if(select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
            if(read(STDIN_FILENO, &c, 1) > 0) {
                if(c == 'q' || c == 27) break;  // q or ESC
                else if(c == ' ') {  // Space - pause
                    paused = !paused;
                    if(cfg.play_sound) play_beep();
                }
                else if(c == 'L' || c == 'l') {  // L - toggle loop
                    cfg.loop = !cfg.loop;
                    if(cfg.play_sound) play_beep();
                }
                else if(c == 'w' || c == 'W') {  // w - increase speed
                    current_speed = min(100.0f, current_speed * 1.1f);
                    if(cfg.play_sound) play_beep();
                }
                else if(c == 's' || c == 'S') {  // s - decrease speed
                    current_speed = max(0.01f, current_speed * 0.9f);
                    if(cfg.play_sound) play_beep();
                }
                else if(c == 'b' && cfg.play_sound) {  // b - manual beep
                    play_beep();
                }
                else if(c == 0x1b) {  // Escape sequence for arrow keys
                    char seq[2];
                    if(read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                        if(seq[0] == '[') {
                            double seek_amount = seek_step;
                            double new_time = current_time;
                            
                            if(seq[1] == 'D') {  // Left arrow
                                new_time = max(0.0, current_time - seek_amount);
                                if(cfg.play_sound) play_sound_effect("seek");
                            }
                            else if(seq[1] == 'C') {  // Right arrow
                                new_time = min(video_info.duration, current_time + seek_amount);
                                if(cfg.play_sound) play_sound_effect("seek");
                            }
                            
                            if (new_time != current_time) {
                                // Seek to new position
                                paused = false;  // Unpause when seeking
                                
                                // Calculate frame number at new time
                                int64_t new_frame = (int64_t)(new_time * video_info.fps);
                                frame_count = new_frame;
                                current_time = new_time;
                                
                                // Seek video
                                if (seek_video(pipe, base_cmd_str, new_time, cfg.out_w, cfg.out_h, cfg.fps)) {
                                    // Read one frame to sync
                                    fread(frame.data(), 1, frame_bytes, pipe);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!paused) {
            auto now = chrono::steady_clock::now();
            double elapsed = chrono::duration<double>(now - last).count();
            double adjusted_frame_dt = base_frame_dt / current_speed;
            
            if(elapsed < adjusted_frame_dt) 
                this_thread::sleep_for(chrono::duration<double>(adjusted_frame_dt - elapsed));
            last = chrono::steady_clock::now();
        } else {
            // When paused, just sleep a bit to reduce CPU usage
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }

    pclose(pipe);
    restore_term();
    
    if(cfg.play_sound) play_sound_effect("end");
    
    cout << "\n";
    return 0;
}
