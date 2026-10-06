#include "gui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include <string>
#include <vector>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <windows.h>
#include <d3d9.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include <wininet.h>
#pragma comment(lib, "wininet.lib")

#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

// ============================================================
//  Animated Texture support (GIF multi-frame + static images)
// ============================================================
struct AnimatedTexture {
    std::vector<IDirect3DTexture9*> frames;
    std::vector<int> delays;   // delay per frame in ms
    int totalDuration = 0;     // sum of all delays
    int width = 0;
    int height = 0;

    AnimatedTexture() = default;
    ~AnimatedTexture() {
        for (auto* tex : frames) {
            if (tex) tex->Release();
        }
    }
    AnimatedTexture(const AnimatedTexture&) = delete;
    AnimatedTexture& operator=(const AnimatedTexture&) = delete;

    AnimatedTexture(AnimatedTexture&& other) noexcept {
        frames = std::move(other.frames);
        delays = std::move(other.delays);
        totalDuration = other.totalDuration;
        width = other.width;
        height = other.height;
        other.frames.clear();
    }
    AnimatedTexture& operator=(AnimatedTexture&& other) noexcept {
        if (this != &other) {
            for (auto* tex : frames) {
                if (tex) tex->Release();
            }
            frames = std::move(other.frames);
            delays = std::move(other.delays);
            totalDuration = other.totalDuration;
            width = other.width;
            height = other.height;
            other.frames.clear();
        }
        return *this;
    }
};

struct TextureLoadTask {
    std::string key;        // cache key (URL or file path)
    std::string url;
    std::string profileName;
    bool isUrl;

    // Output — static image
    unsigned char* imageData = nullptr;
    int width = 0;
    int height = 0;

    // Output — animated GIF
    bool isAnimatedGif = false;
    unsigned char* gifData = nullptr;  // all frames concatenated (RGBA)
    int* gifDelays = nullptr;
    int gifFrameCount = 0;
    int gifWidth = 0;
    int gifHeight = 0;

    bool readyToCreate = false;
};

static std::vector<TextureLoadTask*> g_TextureTasks;
static std::mutex g_TextureTasksMutex;

static std::unordered_map<std::string, AnimatedTexture> g_TextureCache;
static std::mutex g_TextureCacheMutex;
static std::unordered_set<std::string> g_LoadingUrls;
static std::mutex g_LoadingUrlsMutex;

// Helper: read entire file into memory buffer
static bool ReadFileToBuffer(const char* path, std::vector<unsigned char>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    size_t rd = fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    return rd == (size_t)sz;
}

// Check GIF magic bytes
static bool IsGifBuffer(const unsigned char* buf, size_t len) {
    if (len < 6) return false;
    return (memcmp(buf, "GIF89a", 6) == 0 || memcmp(buf, "GIF87a", 6) == 0);
}

// Thread worker — downloads image and decodes (supports animated GIF)
static void TextureDownloadThread(TextureLoadTask* task) {
    std::vector<unsigned char> fileBuffer;

    if (task->isUrl) {
        char tempPath[MAX_PATH], tempFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        GetTempFileNameA(tempPath, "IMG", 0, tempFile);

        if (URLDownloadToFileA(NULL, task->url.c_str(), tempFile, 0, NULL) == S_OK) {
            ReadFileToBuffer(tempFile, fileBuffer);
            DeleteFileA(tempFile);
        }
    } else {
        std::string path = "models/txd/" + task->profileName;
        ReadFileToBuffer(path.c_str(), fileBuffer);
    }

    if (!fileBuffer.empty()) {
        // Try animated GIF first
        if (IsGifBuffer(fileBuffer.data(), fileBuffer.size())) {
            int* delays = nullptr;
            int x = 0, y = 0, z = 0, comp = 0;
            unsigned char* frames = stbi_load_gif_from_memory(
                fileBuffer.data(), (int)fileBuffer.size(),
                &delays, &x, &y, &z, &comp, 4);
            if (frames && z > 1) {
                // Multi-frame GIF
                task->isAnimatedGif = true;
                task->gifData = frames;
                task->gifDelays = delays;
                task->gifFrameCount = z;
                task->gifWidth = x;
                task->gifHeight = y;
            } else if (frames && z == 1) {
                // Single-frame GIF — treat as static
                task->imageData = frames;
                task->width = x;
                task->height = y;
                if (delays) free(delays);
            } else {
                if (frames) stbi_image_free(frames);
                if (delays) free(delays);
            }
        } else {
            // Non-GIF: PNG, JPG, etc.
            task->imageData = stbi_load_from_memory(
                fileBuffer.data(), (int)fileBuffer.size(),
                &task->width, &task->height, NULL, 4);
        }
    }
    task->readyToCreate = true;
}

static void ClearTextureCache() {
    std::lock_guard<std::mutex> lock(g_TextureCacheMutex);
    g_TextureCache.clear(); // Destructors will release textures
}

static void RequestTextureLoad(const std::string& pathOrUrl) {
    if (pathOrUrl.empty()) return;

    {
        std::lock_guard<std::mutex> lock(g_TextureCacheMutex);
        if (g_TextureCache.count(pathOrUrl)) return;
    }
    {
        std::lock_guard<std::mutex> lock(g_LoadingUrlsMutex);
        if (g_LoadingUrls.count(pathOrUrl)) return;
        g_LoadingUrls.insert(pathOrUrl);
    }

    TextureLoadTask* task = new TextureLoadTask();
    task->key = pathOrUrl;

    // Detect URL automatically
    if (pathOrUrl.find("http://") == 0 || pathOrUrl.find("https://") == 0) {
        task->isUrl = true;
        task->url = pathOrUrl;
    } else {
        task->isUrl = false;
        task->profileName = pathOrUrl;
    }

    std::lock_guard<std::mutex> lock(g_TextureTasksMutex);
    g_TextureTasks.push_back(task);
    std::thread(TextureDownloadThread, task).detach();
}

// Helper: create a single D3D texture from RGBA pixel data
static IDirect3DTexture9* CreateTextureFromRGBA(IDirect3DDevice9* pDevice, unsigned char* src, int w, int h) {
    IDirect3DTexture9* texture = nullptr;
    if (pDevice->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) != D3D_OK)
        return nullptr;
    D3DLOCKED_RECT rect;
    if (texture->LockRect(0, &rect, NULL, 0) == D3D_OK) {
        unsigned char* dest = (unsigned char*)rect.pBits;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                dest[x * 4 + 0] = src[x * 4 + 2]; // B
                dest[x * 4 + 1] = src[x * 4 + 1]; // G
                dest[x * 4 + 2] = src[x * 4 + 0]; // R
                dest[x * 4 + 3] = src[x * 4 + 3]; // A
            }
            dest += rect.Pitch;
            src += w * 4;
        }
        texture->UnlockRect(0);
    }
    return texture;
}

// Get the current frame texture for an animated texture based on elapsed time
static IDirect3DTexture9* GetCurrentFrame(const AnimatedTexture& anim) {
    if (anim.frames.empty()) return nullptr;
    if (anim.frames.size() == 1) return anim.frames[0];
    if (anim.totalDuration <= 0) return anim.frames[0];

    DWORD64 now = GetTickCount64();
    int elapsed = (int)(now % (DWORD64)anim.totalDuration);
    int accum = 0;
    for (size_t i = 0; i < anim.frames.size(); i++) {
        accum += anim.delays[i];
        if (elapsed < accum) return anim.frames[i];
    }
    return anim.frames.back();
}

static bool ProcessTextureTasks(IDirect3DDevice9* pDevice) {
    if (g_TextureTasks.empty()) return false;
    bool newlyLoaded = false;
    std::lock_guard<std::mutex> lock(g_TextureTasksMutex);
    for (auto it = g_TextureTasks.begin(); it != g_TextureTasks.end(); ) {
        TextureLoadTask* task = *it;
        if (task->readyToCreate) {
            AnimatedTexture anim;
            bool success = false;

            if (task->isAnimatedGif && task->gifData) {
                // Multi-frame animated GIF
                int frameSize = task->gifWidth * task->gifHeight * 4;
                anim.width = task->gifWidth;
                anim.height = task->gifHeight;
                for (int i = 0; i < task->gifFrameCount; i++) {
                    unsigned char* frameSrc = task->gifData + i * frameSize;
                    IDirect3DTexture9* tex = CreateTextureFromRGBA(pDevice, frameSrc, task->gifWidth, task->gifHeight);
                    if (tex) {
                        anim.frames.push_back(tex);
                        int delay = task->gifDelays ? (task->gifDelays[i] * 10) : 100;
                        if (delay <= 0) delay = 100; // default 100ms for 0-delay frames
                        anim.delays.push_back(delay);
                        anim.totalDuration += delay;
                    }
                }
                stbi_image_free(task->gifData);
                free(task->gifDelays);
                success = !anim.frames.empty();
            } else if (task->imageData) {
                // Single static image (PNG, JPG, single-frame GIF, etc.)
                IDirect3DTexture9* tex = CreateTextureFromRGBA(pDevice, task->imageData, task->width, task->height);
                if (tex) {
                    anim.frames.push_back(tex);
                    anim.delays.push_back(0);
                    anim.totalDuration = 0;
                    anim.width = task->width;
                    anim.height = task->height;
                    success = true;
                }
                stbi_image_free(task->imageData);
            }

            if (success) {
                std::lock_guard<std::mutex> cacheLock(g_TextureCacheMutex);
                g_TextureCache[task->key] = std::move(anim);
                newlyLoaded = true;
            }
            {
                std::lock_guard<std::mutex> loadLock(g_LoadingUrlsMutex);
                g_LoadingUrls.erase(task->key);
            }
            delete task;
            it = g_TextureTasks.erase(it);
        } else {
            ++it;
        }
    }
    return newlyLoaded;
}


// Returns true if the string is valid UTF-8
static bool IsValidUtf8(const char* s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        if (*p < 0x80) { ++p; continue; }
        int extra;
        if      ((*p & 0xE0) == 0xC0) extra = 1;
        else if ((*p & 0xF0) == 0xE0) extra = 2;
        else if ((*p & 0xF8) == 0xF0) extra = 3;
        else return false; // invalid lead byte
        ++p;
        for (int i = 0; i < extra; ++i) {
            if ((*p & 0xC0) != 0x80) return false;
            ++p;
        }
    }
    return true;
}

static std::string SafeToUtf8(const char* raw) {
    if (!raw || raw[0] == '\0') return "";

    // If already valid UTF-8, use as-is (avoid double-converting)
    if (IsValidUtf8(raw)) return std::string(raw);

    // Otherwise assume CP874 (TIS-620 / Windows Thai)
    int wlen = MultiByteToWideChar(874, 0, raw, -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(874, 0, raw, -1, &wstr[0], wlen);
        int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (ulen > 0) {
            std::string result(ulen - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], ulen, nullptr, nullptr);
            return result;
        }
    }

    return std::string(raw);
}

// Detect if char is part of a "word" (not space and not Thai vowel/tone mark)
static bool IsWordChar(unsigned char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return false;
    return true;
}

// Reorder color tags: if a {RRGGBB} or {NAME} tag appears mid-word,
// move it forward to the next whitespace boundary so colors apply to whole words.
static std::string ReorderColorTags(const std::string& input) {
    if (input.empty()) return input;
    if (input.find("TW|") == 0 || input.find("{") != std::string::npos && input.find("}TW|") != std::string::npos) {
        // Do not reorder tags for Twitter format, as it breaks the | delimiters
        return input;
    }

    // Tokenize into segments: text chunks + color tags
    struct Token { bool isTag; std::string data; };
    std::vector<Token> tokens;
    const char* p = input.c_str();
    std::string buffer;
    while (*p) {
        if (*p == '{') {
            // Try to parse tag
            const char* start = p;
            ImVec4 dummy;
            if (ChatColor::parse_color_tag(p, dummy)) {
                if (!buffer.empty()) { tokens.push_back({false, buffer}); buffer.clear(); }
                tokens.push_back({true, std::string(start, p - start)});
                continue;
            }
        }
        buffer += *p++;
    }
    if (!buffer.empty()) tokens.push_back({false, buffer});

    // For each tag, check if it sits mid-word (prev ends with non-space,
    // next begins with non-space). If so, move the tag forward to the next
    // whitespace boundary (handles both ASCII and UTF-8 Thai text).
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (!tokens[i].isTag) continue;

        // Check prev token ends with non-whitespace
        bool prevNonSpace = false;
        if (i > 0 && !tokens[i-1].isTag && !tokens[i-1].data.empty()) {
            unsigned char lastByte = static_cast<unsigned char>(tokens[i-1].data.back());
            if (lastByte != ' ' && lastByte != '\t' && lastByte != '\n' && lastByte != '\r') {
                prevNonSpace = true;
            }
        }
        if (!prevNonSpace) continue;

        if (i + 1 >= tokens.size() || tokens[i+1].isTag) continue;
        std::string& next = tokens[i+1].data;
        if (next.empty()) continue;
        unsigned char firstByte = static_cast<unsigned char>(next[0]);
        if (firstByte == ' ' || firstByte == '\t' || firstByte == '\n' || firstByte == '\r') continue;

        // Determine type by prev's last byte: ASCII or non-ASCII (Thai)
        unsigned char prevLast = static_cast<unsigned char>(tokens[i-1].data.back());
        bool prevIsAscii = (prevLast < 0x80);

        size_t splitPos = 0;
        if (prevIsAscii) {
            // Walk through ASCII chunk (including internal spaces) until we hit non-ASCII or end
            while (splitPos < next.size()) {
                unsigned char b = static_cast<unsigned char>(next[splitPos]);
                if (b >= 0x80) break;
                splitPos++;
            }
            // Back up over trailing whitespace so tag sits right after last ASCII word
            while (splitPos > 0) {
                unsigned char b = static_cast<unsigned char>(next[splitPos - 1]);
                if (b == ' ' || b == '\t' || b == '\n' || b == '\r') splitPos--;
                else break;
            }
        } else {
            // Thai cluster: walk forward (handle UTF-8) until we hit a whitespace
            while (splitPos < next.size()) {
                unsigned char b = static_cast<unsigned char>(next[splitPos]);
                if (b == ' ' || b == '\t' || b == '\n' || b == '\r') break;
                if (b >= 0xC0) {
                    int extra = 0;
                    if ((b & 0xE0) == 0xC0) extra = 1;
                    else if ((b & 0xF0) == 0xE0) extra = 2;
                    else if ((b & 0xF8) == 0xF0) extra = 3;
                    splitPos += 1 + extra;
                } else {
                    splitPos++;
                }
            }
        }
        if (splitPos == 0 || splitPos >= next.size()) continue;

        // Move the word part (next[0..splitPos]) before the tag
        std::string moved = next.substr(0, splitPos);
        next.erase(0, splitPos);
        tokens[i-1].data += moved;
    }

    // Reassemble
    std::string out;
    for (auto& t : tokens) out += t.data;
    return out;
}

extern HMODULE g_hModule;

static std::string GetModuleDirectory() {
    char path[MAX_PATH];
    if (GetModuleFileNameA(g_hModule, path, MAX_PATH)) {
        PathRemoveFileSpecA(path);
        return std::string(path);
    }
    return "";
}

static std::string GetCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm  = std::localtime(&now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S ", tm);
    return std::string(buf);
}

// ============================================================
//  SA-MP config (sa-mp.cfg) reader/writer
//  Uses SHGetFolderPathW to find the real Documents folder.
//  Windows handles OneDrive redirection and localized folder
//  names (e.g. Thai "เอกสาร") automatically via this API.
// ============================================================
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

static std::wstring g_CachedCfgPath;  // cached after first successful find

static std::wstring FindSampCfgPath() {
    const wchar_t* subPath = L"\\GTA San Andreas User Files\\SAMP\\sa-mp.cfg";
    std::vector<std::wstring> candidates;

    // 1) SHGetFolderPathW(CSIDL_PERSONAL) — the most reliable method.
    //    Windows returns the correct "My Documents" path regardless of
    //    OneDrive sync, folder redirection, or localized folder names.
    {
        wchar_t docs[MAX_PATH] = {0};
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs))) {
            candidates.push_back(std::wstring(docs) + subPath);
        }
    }

    // 2) Game directory — some portable/custom installs put sa-mp.cfg
    //    next to gta_sa.exe (same folder where our ASI is loaded from).
    {
        wchar_t modPath[MAX_PATH] = {0};
        if (GetModuleFileNameW(g_hModule, modPath, MAX_PATH)) {
            PathRemoveFileSpecW(modPath);
            candidates.push_back(std::wstring(modPath) + L"\\sa-mp.cfg");
        }
    }

    // 3) USERPROFILE fallback — covers edge cases where shell APIs fail
    //    or Documents is in a non-standard location.
    {
        wchar_t userProfile[MAX_PATH] = {0};
        DWORD len = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring up(userProfile);
            candidates.push_back(up + L"\\Documents" + subPath);
            candidates.push_back(up + L"\\OneDrive\\Documents" + subPath);
        }
    }

    // Pick the first path that actually exists on disk
    for (const auto& p : candidates) {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            OutputDebugStringA(("[ChatImGui] Found sa-mp.cfg\n"));
            return p;
        }
    }

    OutputDebugStringA("[ChatImGui] sa-mp.cfg not found in any candidate path\n");
    return L"";
}

static const std::wstring& GetSampCfgPath() {
    if (g_CachedCfgPath.empty()) {
        g_CachedCfgPath = FindSampCfgPath();
    }
    return g_CachedCfgPath;
}

static int ReadSampCfgInt(const std::string& key, int defaultVal) {
    const std::wstring& cfgPath = GetSampCfgPath();
    if (cfgPath.empty()) return defaultVal;

    std::ifstream file(cfgPath);  // MSVC supports wstring path
    if (!file.is_open()) return defaultVal;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(key + "=", 0) == 0) {
            std::string val = line.substr(key.size() + 1);
            try { return std::stoi(val); }
            catch (...) { return defaultVal; }
        }
    }
    return defaultVal;
}

static void WriteSampCfgInt(const std::string& key, int value) {
    const std::wstring& cfgPath = GetSampCfgPath();
    if (cfgPath.empty()) return;

    // Read all lines
    std::vector<std::string> lines;
    bool found = false;
    {
        std::ifstream file(cfgPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind(key + "=", 0) == 0) {
                    line = key + "=" + std::to_string(value);
                    found = true;
                }
                lines.push_back(line);
            }
        }
    }
    if (!found) {
        lines.push_back(key + "=" + std::to_string(value));
    }

    // Write back
    std::ofstream out(cfgPath, std::ios::trunc);
    if (out.is_open()) {
        for (const auto& l : lines) {
            out << l << "\n";
        }
    }
}

void SAMPChatImGui::Init(HWND hwnd, IDirect3DDevice9* device) {
    if (m_initialized) return;
    m_hwnd = hwnd;

    // Read timestamp setting from sa-mp.cfg
    int tsVal = ReadSampCfgInt("timestamp", 0);
    showTimestamp = (tsVal != 0);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(device);

    BuildFonts();
    ApplyTheme();
    SnapshotHistory();

    m_alpha     = fade.minAlpha;
    m_fadeState = FadeState::Idle;
    m_lastTick  = GetTickCount64();
    m_initialized = true;
}

void SAMPChatImGui::Shutdown() {
    if (!m_initialized) return;
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

// Recursively create directories (handles multi-level paths like fonts\cache)
static void CreateDirectoryRecursive(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find_first_of("\\/", pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        CreateDirectoryA(sub.c_str(), NULL);
    }
    CreateDirectoryA(path.c_str(), NULL);
}

// Download a font from a URL to a local cached file; returns the local path or empty on failure.
static std::string DownloadFontFromUrl(const std::string& url, const std::string& cacheDir) {
    if (url.empty()) return "";

    // Create cache directory if it doesn't exist (recursive)
    CreateDirectoryRecursive(cacheDir);

    // Generate a simple hash-based filename from the URL to avoid re-downloading
    unsigned int hash = 0;
    for (char c : url) hash = hash * 31 + (unsigned char)c;
    char hashBuf[32];
    snprintf(hashBuf, sizeof(hashBuf), "font_%08X", hash);

    // Detect extension from URL (default to .ttf)
    std::string ext = ".ttf";
    {
        size_t dotPos = url.rfind('.');
        size_t slashPos = url.rfind('/');
        size_t qPos = url.find('?', dotPos != std::string::npos ? dotPos : 0);
        if (dotPos != std::string::npos && (slashPos == std::string::npos || dotPos > slashPos)) {
            std::string urlExt = url.substr(dotPos, (qPos != std::string::npos ? qPos : url.size()) - dotPos);
            if (urlExt == ".ttf" || urlExt == ".otf" || urlExt == ".ttc") ext = urlExt;
        }
    }

    std::string localPath = cacheDir + "\\" + hashBuf + ext;

    // If already downloaded, use cached version
    if (GetFileAttributesA(localPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        OutputDebugStringA(("[ChatImGui] Using cached font: " + localPath + "\n").c_str());
        return localPath;
    }

    // Download
    OutputDebugStringA(("[ChatImGui] Downloading font from: " + url + "\n").c_str());
    HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), localPath.c_str(), 0, NULL);
    if (hr == S_OK && GetFileAttributesA(localPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        OutputDebugStringA(("[ChatImGui] Font downloaded to: " + localPath + "\n").c_str());
        return localPath;
    }

    OutputDebugStringA(("[ChatImGui] Failed to download font from: " + url + "\n").c_str());
    DeleteFileA(localPath.c_str()); // Clean up partial download
    return "";
}

// HTTP GET a URL and return the response body as a string (using WinINet for custom User-Agent).
static std::string HttpGetString(const std::string& url, const std::string& userAgent) {
    std::string result;
    HINTERNET hInternet = InternetOpenA(userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return result;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (hUrl) {
        char buffer[4096];
        DWORD bytesRead;
        while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result.append(buffer, bytesRead);
        }
        InternetCloseHandle(hUrl);
    }
    InternetCloseHandle(hInternet);
    return result;
}

// Extract Google Font family name from various inputs:
//   "Kanit"                                      -> "Kanit"
//   "Noto Sans Thai"                              -> "Noto Sans Thai"
//   "https://fonts.google.com/specimen/Kanit"     -> "Kanit"
//   "https://fonts.googleapis.com/css?family=Kanit" -> "Kanit"
// Returns empty string if not a Google Font input.
static std::string ExtractGoogleFontFamily(const std::string& input) {
    // Google Fonts specimen URL
    {
        const std::string marker = "fonts.google.com/specimen/";
        size_t pos = input.find(marker);
        if (pos != std::string::npos) {
            std::string name = input.substr(pos + marker.size());
            // Remove query string
            size_t q = name.find('?');
            if (q != std::string::npos) name = name.substr(0, q);
            // URL decode '+' to space
            for (auto& c : name) if (c == '+') c = ' ';
            return name;
        }
    }
    // Google Fonts CSS API URL
    {
        const std::string marker = "family=";
        size_t pos = input.find(marker);
        if (pos != std::string::npos && input.find("fonts.googleapis.com") != std::string::npos) {
            std::string name = input.substr(pos + marker.size());
            // Remove weight specifier like :wght@400 and query params
            size_t colon = name.find(':');
            if (colon != std::string::npos) name = name.substr(0, colon);
            size_t amp = name.find('&');
            if (amp != std::string::npos) name = name.substr(0, amp);
            for (auto& c : name) if (c == '+') c = ' ';
            return name;
        }
    }
    return "";
}

// Download a Google Font by family name, returns local cached .ttf path.
static std::string DownloadGoogleFont(const std::string& familyName, const std::string& cacheDir) {
    if (familyName.empty()) return "";

    CreateDirectoryRecursive(cacheDir);

    // Generate cache filename from family name
    unsigned int hash = 0;
    for (char c : familyName) hash = hash * 31 + (unsigned char)c;
    char hashBuf[32];
    snprintf(hashBuf, sizeof(hashBuf), "gfont_%08X.ttf", hash);
    std::string localPath = cacheDir + "\\" + hashBuf;

    // Check cache
    if (GetFileAttributesA(localPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        OutputDebugStringA(("[ChatImGui] Using cached Google Font: " + localPath + "\n").c_str());
        return localPath;
    }

    // URL-encode family name: replace spaces with +
    std::string encodedName = familyName;
    for (auto& c : encodedName) if (c == ' ') c = '+';

    // Use CSS v1 API — returns a single full font file (all glyphs, not subset).
    // An old Android User-Agent makes Google serve .ttf instead of .woff2 or .eot.
    std::string cssUrl = "https://fonts.googleapis.com/css?family=" + encodedName;
    std::string userAgent = "Mozilla/5.0 (Linux; U; Android 4.1.1; en-gb; Build/KLP) AppleWebKit/534.30 (KHTML, like Gecko) Version/4.0 Safari/534.30";

    OutputDebugStringA(("[ChatImGui] Fetching Google Fonts CSS: " + cssUrl + "\n").c_str());
    std::string css = HttpGetString(cssUrl, userAgent);

    if (css.empty()) {
        OutputDebugStringA("[ChatImGui] Failed to fetch Google Fonts CSS\n");
        return "";
    }

    // Parse CSS to find the .ttf URL
    // The CSS v1 response looks like:
    //   @font-face { ... src: url(https://fonts.gstatic.com/s/.../font.ttf) format('truetype'); }
    std::string fontUrl;
    size_t pos = 0;
    while ((pos = css.find("url(", pos)) != std::string::npos) {
        pos += 4;
        size_t end = css.find(")", pos);
        if (end == std::string::npos) break;
        std::string url = css.substr(pos, end - pos);
        // Strip surrounding quotes if present
        if (!url.empty() && (url.front() == '\'' || url.front() == '"')) url.erase(0, 1);
        if (!url.empty() && (url.back() == '\'' || url.back() == '"')) url.pop_back();
        fontUrl = url;
        break;
    }

    if (fontUrl.empty()) {
        OutputDebugStringA(("[ChatImGui] No URL found in Google Fonts CSS. CSS content: " + css.substr(0, 200) + "\n").c_str());
        return "";
    }

    // Download the actual .ttf file
    OutputDebugStringA(("[ChatImGui] Downloading Google Font .ttf: " + fontUrl + "\n").c_str());
    HRESULT hr = URLDownloadToFileA(NULL, fontUrl.c_str(), localPath.c_str(), 0, NULL);
    if (hr == S_OK && GetFileAttributesA(localPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        OutputDebugStringA(("[ChatImGui] Google Font saved to: " + localPath + "\n").c_str());
        return localPath;
    }

    OutputDebugStringA("[ChatImGui] Failed to download Google Font .ttf\n");
    DeleteFileA(localPath.c_str());
    return "";
}

void SAMPChatImGui::BuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Latin + Thai Unicode ranges
    static const ImWchar all_ranges[] = {
        0x0020, 0x00FF,  // Basic Latin + Latin Supplement
        0x0E00, 0x0E7F,  // Thai
        0,
    };

    std::string modDir = GetModuleDirectory();
    std::string fontPath = customFontPath.empty() ? (modDir + "\\Font\\Prompt-Bold.ttf") : customFontPath;

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;

    m_chatFont = nullptr;
    if (GetFileAttributesA(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        fopen_s(&f, fontPath.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (fsize > 0) {
                void* fontData = ImGui::MemAlloc(fsize);
                if (fontData && fread(fontData, 1, fsize, f) == (size_t)fsize) {
                    m_chatFont = io.Fonts->AddFontFromMemoryTTF(fontData, fsize, fontSize, &cfg, all_ranges);
                } else {
                    if (fontData) ImGui::MemFree(fontData);
                }
            }
            fclose(f);
        }
    }
    
    // If font loading failed (e.g. invalid file, or fopen failed due to Unicode path), fallback to default
    if (!m_chatFont) {
        m_chatFont = io.Fonts->AddFontDefault();
    }

    // Merge Windows Emoji Font
    std::string windir = getenv("WINDIR") ? getenv("WINDIR") : "C:\\Windows";
    std::string emojiPath = windir + "\\Fonts\\seguiemj.ttf";
    if (GetFileAttributesA(emojiPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        static const ImWchar emoji_ranges[] = {
            0x2000, 0x2BFF,     // Symbols, Arrows, Box Drawing, Dingbats
            0x1F000, 0x1FAFF,   // Emojis, Symbols, Pictographs
            0
        };
        FILE* f = nullptr;
        fopen_s(&f, emojiPath.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (fsize > 0) {
                void* emojiData = ImGui::MemAlloc(fsize);
                if (emojiData && fread(emojiData, 1, fsize, f) == (size_t)fsize) {
                    ImFontConfig emojiCfg;
                    emojiCfg.MergeMode = true;
                    emojiCfg.OversampleH = 1;
                    emojiCfg.OversampleV = 1;
                    emojiCfg.FontDataOwnedByAtlas = true;
                    io.Fonts->AddFontFromMemoryTTF(emojiData, fsize, fontSize, &emojiCfg, emoji_ranges);
                } else {
                    if (emojiData) ImGui::MemFree(emojiData);
                }
            }
            fclose(f);
        }
    }

    io.Fonts->Build();
    m_fontsBuilt = true;
}

void SAMPChatImGui::ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 10.f;
    s.ChildRounding     = 8.f;
    s.FrameRounding     = 6.f;
    s.ScrollbarRounding = 6.f;
    s.WindowBorderSize  = 0.f;
    s.WindowPadding     = {12.f, 10.f};
    s.ItemSpacing       = {6.f, 4.f};
    s.FramePadding      = {10.f, 6.f};
    s.ScrollbarSize     = 6.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.12f, 0.88f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.08f, 0.08f, 0.10f, 0.60f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.06f, 0.06f, 0.08f, 0.30f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.18f, 0.18f, 0.22f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.25f, 0.30f, 0.90f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.90f, 0.92f, 1.f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.52f, 1.f);
    c[ImGuiCol_Border]               = ImVec4(0.15f, 0.15f, 0.18f, 0.40f);
    c[ImGuiCol_Separator]            = ImVec4(0.15f, 0.15f, 0.18f, 0.50f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.12f, 0.14f, 0.75f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.16f, 0.16f, 0.19f, 0.85f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.20f, 0.24f, 0.95f);
    c[ImGuiCol_Button]               = ImVec4(0.70f, 0.15f, 0.15f, 0.85f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.85f, 0.20f, 0.20f, 0.95f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.95f, 0.25f, 0.25f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.85f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.70f, 0.15f, 0.15f, 0.85f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.85f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.70f, 0.15f, 0.15f, 0.55f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.85f, 0.20f, 0.20f, 0.70f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.95f, 0.25f, 0.25f, 0.85f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.15f, 0.95f);
}

void SAMPChatImGui::SnapshotHistory() {
    samp_utils::ProcessChatHistory([this](auto* pChat) {
        if (!pChat) return;

        ClearTextureCache();
        m_lines.clear();

        for (int i = 0; i < 100; ++i) {
            auto& entry = pChat->m_entry[i];
            if (entry.m_szText[0] == '\0') continue;

            ChatLine line;
            line.type = entry.m_nType;
            line.prefix = SafeToUtf8(entry.m_szPrefix);
            
            std::string safeText = ReorderColorTags(SafeToUtf8(entry.m_szText));
            if (safeText.find("TW|") == 0) {
                line.isTweet = true;
                size_t pos = 3;
                // Collect all fields separated by '|'
                std::vector<std::string> fields;
                while (pos < safeText.length()) {
                    size_t nextPos = safeText.find('|', pos);
                    if (nextPos != std::string::npos) {
                        fields.push_back(safeText.substr(pos, nextPos - pos));
                        pos = nextPos + 1;
                    } else {
                        fields.push_back(safeText.substr(pos));
                        pos = safeText.length();
                    }
                }
                // Assign fields robustly — missing fields stay empty
                if (fields.size() > 0) line.tweetName       = fields[0];
                if (fields.size() > 1) line.tweetTime       = fields[1];
                if (fields.size() > 2) line.tweetProfilePic = fields[2];
                if (fields.size() > 3) line.tweetText       = fields[3];
                if (fields.size() > 4) line.tweetImageUrl   = fields[4];
                line.text = "";
            } else {
                line.text = safeText;
            }

            line.prefixColor = ChatColor::from_samp(entry.m_prefixColor);
            line.textColor   = ChatColor::from_samp(entry.m_textColor);
            if (showTimestamp)
                line.timestamp = GetCurrentTimestamp();
            m_lines.push_back(std::move(line));
        }
    });
    m_scrollToBot = true;
}

void SAMPChatImGui::OnNewEntry(int type, const char* szText, const char* szPrefix,
                                DWORD textColor, DWORD prefixColor) {
    // Debug: log raw szText bytes
    if (szText) {
        std::string dbg = "[ChatImGui] szText: ";
        for (int i = 0; szText[i] && i < 150; ++i) {
            unsigned char b = static_cast<unsigned char>(szText[i]);
            if (b >= 0x20 && b < 0x7F) {
                dbg += static_cast<char>(b);
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "[%02X]", b);
                dbg += buf;
            }
        }
        dbg += "\n";
        OutputDebugStringA(dbg.c_str());
    }

    ChatLine line;
    line.type        = type;
    
    std::string safeText = ReorderColorTags(SafeToUtf8(szText ? szText : ""));
    
    // Parse Twitter Message Format: TW|Name|Time|ProfilePic|Text|ImageURL
    if (safeText.find("TW|") == 0) {
        line.isTweet = true;
        size_t pos = 3;
        // Collect all fields separated by '|'
        std::vector<std::string> fields;
        while (pos < safeText.length()) {
            size_t nextPos = safeText.find('|', pos);
            if (nextPos != std::string::npos) {
                fields.push_back(safeText.substr(pos, nextPos - pos));
                pos = nextPos + 1;
            } else {
                fields.push_back(safeText.substr(pos));
                pos = safeText.length();
            }
        }
        // Assign fields robustly — missing fields stay empty
        if (fields.size() > 0) line.tweetName       = fields[0];
        if (fields.size() > 1) line.tweetTime       = fields[1];
        if (fields.size() > 2) line.tweetProfilePic = fields[2];
        if (fields.size() > 3) line.tweetText       = fields[3];
        if (fields.size() > 4) line.tweetImageUrl   = fields[4];
        line.text = ""; // clear main text since we use custom rendering
    } else {
        line.text = safeText;
    }
    
    line.prefix      = SafeToUtf8(szPrefix ? szPrefix : "");
    line.textColor   = ChatColor::from_samp(textColor);
    line.prefixColor = ChatColor::from_samp(prefixColor);
    if (showTimestamp)
        line.timestamp = GetCurrentTimestamp();

    if ((int)m_lines.size() >= MAX_LINES) {
        m_lines.pop_front();
    }
    m_lines.push_back(std::move(line));

    m_scrollToBot = true;

    if (m_fadeState == FadeState::Idle || m_fadeState == FadeState::FadingOut)
        m_fadeState = FadeState::FadingIn;
    else if (m_fadeState == FadeState::Holding) {
        m_fadeState  = FadeState::Holding;
        m_holdTimer  = fade.holdSeconds;
    }
}

void SAMPChatImGui::OnInputOpen() {
    // Prevent reopening immediately after close (e.g. Enter key bouncing)
    if (IsInputCooldown()) {
        samp_utils::CloseInputBox();
        return;
    }

    m_inputOpen  = true;
    m_holdTimer  = fade.chatOpenSeconds;
    m_fadeState  = FadeState::FadingIn;
    m_focusInput = true;
    memset(m_inputBuf, 0, sizeof(m_inputBuf));
    m_nHistoryIndex = -1;
    memset(m_szSavedInput, 0, sizeof(m_szSavedInput));
    m_scrollDelta = 0;

    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();

    samp_utils::SetCursorMode(2 /*CURSOR_LOCKCAMANDCONTROL*/, false);
}

void SAMPChatImGui::OnInputClose() {
    m_inputOpen = false;
    m_fadeState = FadeState::Holding;
    m_holdTimer = fade.holdSeconds;
    m_scrollDelta = 0;
    m_inputCloseTime = GetTickCount64();
    m_showSettings = false;

    // Clear pending key state so stray Enter/T don't trigger anything
    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();
    memset(m_inputBuf, 0, sizeof(m_inputBuf));

    samp_utils::SetCursorMode(0 /*CURSOR_NONE*/, false);
}

void SAMPChatImGui::UpdateFade(float dt) {
    switch (m_fadeState) {
    case FadeState::FadingIn:
        m_alpha += fade.fadeInSpeed * dt;
        if (m_alpha >= fade.maxAlpha) {
            m_alpha     = fade.maxAlpha;
            m_fadeState = FadeState::Holding;
            m_holdTimer = m_inputOpen ? fade.chatOpenSeconds : fade.holdSeconds;
        }
        break;
    case FadeState::Holding:
        if (!m_inputOpen) {
            m_holdTimer -= dt;
            if (m_holdTimer <= 0.f)
                m_fadeState = FadeState::FadingOut;
        }
        break;
    case FadeState::FadingOut:
        m_alpha -= fade.fadeOutSpeed * dt;
        if (m_alpha <= fade.minAlpha) {
            m_alpha     = fade.minAlpha;
            m_fadeState = FadeState::Idle;
        }
        break;
    case FadeState::Idle:
        break;
    }
}

// Draw text with outline, supporting word-wrap. Updates x/y to end of drawn text.
static void DrawOutlinedTextWrapped(const char* text, const ImVec4& color,
                                     float& x, float& y, float wrapStartX, float wrapWidth,
                                     float lineHeight) {
    if (!text || text[0] == '\0') return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = const_cast<ImFont*>(ImGui::GetFont());
    float fontSize = ImGui::GetFontSize();
    float alpha = color.w;
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
    ImU32 outline = IM_COL32(0, 0, 0, (int)(alpha * 255));

    int totalLen = (int)strlen(text);
    int offset = 0;

    while (offset < totalLen) {
        float remaining = wrapWidth - (x - wrapStartX);
        if (remaining < 5.f) {
            x = wrapStartX;
            y += lineHeight + 2.f;
            remaining = wrapWidth;
        }

        // Word-wrap: measure words until we exceed remaining width
        int fit = 0;
        float accW = 0.f;
        const char* s = text + offset;
        const char* end = text + totalLen;

        while (s < end) {
            const char* wordStart = s;
            while (s < end && *s != ' ') s++;
            const char* wordEnd = s;
            if (s < end) s++; // include trailing space

            float wordW = ImGui::CalcTextSize(wordStart, s, false, 0.0f).x;
            if (accW + wordW > remaining && fit > 0) {
                s = wordStart; // rewind to before this word
                break;
            }
            accW += wordW;
            fit = (int)(s - (text + offset));
        }

        if (fit == 0) {
            // Force at least one UTF-8 character
            fit = 1;
            unsigned char c = (unsigned char)text[offset];
            if ((c & 0xF0) == 0xF0) fit = 4;
            else if ((c & 0xE0) == 0xE0) fit = 3;
            else if ((c & 0xC0) == 0xC0) fit = 2;
        }

        // Draw outline (8 directions)
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                dl->AddText(font, fontSize, ImVec2(x + dx, y + dy), outline,
                            text + offset, text + offset + fit);
            }
        }

        // Draw main text
        dl->AddText(font, fontSize, ImVec2(x, y), col,
                    text + offset, text + offset + fit);

        x += ImGui::CalcTextSize(text + offset, text + offset + fit, false, 0.0f).x;
        offset += fit;

        if (offset < totalLen) {
            x = wrapStartX;
            y += lineHeight + 2.f;
        }
    }
}

void SAMPChatImGui::RenderMessageRow(const ChatLine& c_line, int /*rowIdx*/) {
    ChatLine& line = const_cast<ChatLine&>(c_line);
    const float alpha = m_alpha;

    float availW = ImGui::GetContentRegionAvail().x;
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    float startX = startPos.x;
    float startY = startPos.y;
    float currentX = startX;
    float currentY = startY;
    float lineHeight = ImGui::GetTextLineHeight();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (line.isTweet) {
        // --- Twitter Card Rendering (with animated GIF support) ---
        if (!line.texturesLoaded) {
            if (!line.tweetProfilePic.empty()) RequestTextureLoad(line.tweetProfilePic);
            if (!line.tweetImageUrl.empty()) RequestTextureLoad(line.tweetImageUrl);
            line.texturesLoaded = true;
        }

        // Look up animated textures from global cache every frame
        IDirect3DTexture9* profileTex = nullptr;
        IDirect3DTexture9* imageTex = nullptr;
        const AnimatedTexture* imageAnim = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_TextureCacheMutex);
            if (!line.tweetProfilePic.empty()) {
                auto it = g_TextureCache.find(line.tweetProfilePic);
                if (it != g_TextureCache.end()) profileTex = GetCurrentFrame(it->second);
            }
            if (!line.tweetImageUrl.empty()) {
                auto it = g_TextureCache.find(line.tweetImageUrl);
                if (it != g_TextureCache.end()) {
                    imageTex = GetCurrentFrame(it->second);
                    imageAnim = &it->second;
                }
            }
        }

        float cardW = availW * 0.55f;
        if (cardW > 320.f) cardW = 320.f;
        
        float textW = cardW - 52.f;
        ImVec2 textSize = ImGui::CalcTextSize(line.tweetText.c_str(), nullptr, false, textW);
        float headerH = 30.f;
        float textH = line.tweetText.empty() ? 0.f : (textSize.y + 4.f);

        // Compute image display size with aspect ratio — fill card width, tall
        float imgDisplayW = 0.f;
        float imgDisplayH = 0.f;
        if (imageTex && imageAnim) {
            float texW = (float)imageAnim->width;
            float texH = (float)imageAnim->height;
            float aspect = texH > 0 ? texW / texH : 1.f;

            float imgMaxW = cardW - 52.f;  // start after avatar column
            float imgMaxH = 140.f;          // much taller
            imgDisplayW = imgMaxW;
            imgDisplayH = imgDisplayW / aspect;
            if (imgDisplayH > imgMaxH) {
                imgDisplayH = imgMaxH;
                imgDisplayW = imgDisplayH * aspect;
            }
        }
        float cardH = headerH + textH + imgDisplayH + (imageTex ? 6.f : 4.f);

        // Card background — darker black
        ImU32 bgColor = IM_COL32(8, 8, 8, (int)(alpha * 240));
        dl->AddRectFilled(ImVec2(startX, startY), ImVec2(startX + cardW, startY + cardH), bgColor, 6.f);
        
        // Avatar — centered in the card (no image) or between name and text (with image)
        float avatarRadius = 14.f;
        float avatarY = !line.tweetImageUrl.empty()
            ? (startY + (headerH + textH) * 0.5f)   // center between name and tweet text
            : (startY + cardH * 0.5f);                // center in full card
        ImVec2 avatarCenter(startX + 22.f, avatarY);
        if (profileTex) {
            dl->AddImageRounded((ImTextureID)profileTex, 
                                ImVec2(avatarCenter.x - avatarRadius, avatarCenter.y - avatarRadius),
                                ImVec2(avatarCenter.x + avatarRadius, avatarCenter.y + avatarRadius),
                                ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,(int)(alpha*255)), avatarRadius);
        } else {
            dl->AddCircleFilled(avatarCenter, avatarRadius, IM_COL32(80, 80, 80, (int)(alpha*255)));
        }
        dl->AddCircle(avatarCenter, avatarRadius, IM_COL32(120, 120, 120, (int)(alpha*200)), 24, 1.2f);

        // Name + Time (clip name if too long)
        float headerX = startX + 46.f;
        float headerY = startY + 4.f;
        ImU32 nameCol = IM_COL32(255, 255, 255, (int)(alpha*255));
        ImU32 timeCol = IM_COL32(150, 150, 150, (int)(alpha*255));
        
        std::string displayName = line.tweetName;
        float timeW = ImGui::CalcTextSize(line.tweetTime.c_str()).x;
        float maxNameW = textW - timeW - 10.f;
        if (maxNameW < 20.f) maxNameW = 20.f;
        float nameW = ImGui::CalcTextSize(displayName.c_str()).x;
        if (nameW > maxNameW) {
            while (!displayName.empty() && ImGui::CalcTextSize((displayName + "...").c_str()).x > maxNameW) {
                displayName.pop_back();
            }
            displayName += "...";
            nameW = ImGui::CalcTextSize(displayName.c_str()).x;
        }
        dl->AddText(ImVec2(headerX, headerY), nameCol, displayName.c_str());
        dl->AddText(ImVec2(headerX + nameW + 5.f, headerY), timeCol, line.tweetTime.c_str());

        // Tweet Text
        float textY = headerY + 20.f;
        if (!line.tweetText.empty()) {
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(headerX, textY), 
                        nameCol, line.tweetText.c_str(), nullptr, textW);
        }

        // Attached Image — fill card width with small padding (supports animated GIF)
        if (imageTex) {
            float imgY = startY + headerH + textH;
            float imgX = headerX;
            dl->AddImageRounded((ImTextureID)imageTex, 
                                ImVec2(imgX, imgY), ImVec2(imgX + imgDisplayW, imgY + imgDisplayH),
                                ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,(int)(alpha*255)), 5.f);
        }

        ImGui::Dummy(ImVec2(availW, cardH + 4.f));
        return;
    }

    // Timestamp pill
    float tsW = 0.f;
    if (showTimestamp && !line.timestamp.empty()) {
        ImVec4 tsColor(0.92f, 0.94f, 0.96f, alpha);
        ImVec2 textSize = ImGui::CalcTextSize(line.timestamp.c_str());
        float padX = 5.f;
        float padY = 2.f;
        float rectW = textSize.x + padX * 2.f;
        float rectH = textSize.y + padY * 2.f;
        float rounding = rectH * 0.5f;

        dl->AddRectFilled(
            ImVec2(startX, startY),
            ImVec2(startX + rectW, startY + rectH),
            IM_COL32(0, 0, 0, (int)(alpha * 200)),
            rounding
        );

        float textX = startX + padX;
        float textY = startY + padY;
        ImU32 outline = IM_COL32(0, 0, 0, (int)(alpha * 255));
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                dl->AddText(ImVec2(textX + dx, textY + dy), outline, line.timestamp.c_str());
            }
        }
        dl->AddText(ImVec2(textX, textY), ImGui::ColorConvertFloat4ToU32(tsColor), line.timestamp.c_str());

        tsW = rectW + 4.f;
        currentX = startX + tsW;
    }

    float wrapW = availW - tsW;
    if (wrapW < 50.f) wrapW = availW;
    float wrapStartX = startX + tsW;

    // Prefix (player name)
    if (!line.prefix.empty()) {
        ImVec4 pc = line.prefixColor; pc.w = alpha;
        DrawOutlinedTextWrapped(line.prefix.c_str(), pc,
                                currentX, currentY, wrapStartX, wrapW, lineHeight);

        // Space after prefix
        float spaceW = ImGui::CalcTextSize(" ").x;
        if (currentX + spaceW > wrapStartX + wrapW - 2.f) {
            currentX = wrapStartX;
            currentY += lineHeight + 2.f;
        } else {
            currentX += spaceW;
        }

        // Colon
        ImVec4 colonColor(0.75f, 0.78f, 0.85f, alpha);
        DrawOutlinedTextWrapped(": ", colonColor,
                                currentX, currentY, wrapStartX, wrapW, lineHeight);
    }

    // Message text with inline {RRGGBB} color tags
    const char* p = line.text.c_str();
    
    // Debug: log entire line.text before parsing
    {
        std::string dbg = "[ChatImGui] line.text: ";
        for (int i = 0; i < 200 && p[i]; ++i) {
            unsigned char b = static_cast<unsigned char>(p[i]);
            if (b >= 0x20 && b < 0x7F) {
                dbg += static_cast<char>(b);
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "[%02X]", b);
                dbg += buf;
            }
        }
        dbg += "\n";
        OutputDebugStringA(dbg.c_str());
    }
    
    ImVec4 cur = line.textColor; cur.w = alpha;
    std::string segment;

    auto flush = [&]() {
        if (!segment.empty()) {
            // Debug: log segment being drawn with color
            int r = (int)(cur.x * 255);
            int g = (int)(cur.y * 255);
            int b = (int)(cur.z * 255);
            std::string dbg = "[ChatImGui] Draw segment (color=" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "): ";
            for (int i = 0; i < 80 && segment[i]; ++i) {
                unsigned char by = static_cast<unsigned char>(segment[i]);
                if (by >= 0x20 && by < 0x7F) {
                    dbg += static_cast<char>(by);
                } else {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "[%02X]", by);
                    dbg += buf;
                }
            }
            dbg += "\n";
            OutputDebugStringA(dbg.c_str());
            
            DrawOutlinedTextWrapped(segment.c_str(), cur,
                                    currentX, currentY, wrapStartX, wrapW, lineHeight);
            segment.clear();
        }
    };

    while (*p) {
        ImVec4 nextColor;
        if (ChatColor::parse_color_tag(p, nextColor)) {
            flush();
            nextColor.w = alpha;
            cur = nextColor;
            {
                int r = (int)(cur.x * 255);
                int g = (int)(cur.y * 255);
                int b = (int)(cur.z * 255);
                std::string dbg = "[ChatImGui] Parsed tag -> color(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ")\n";
                OutputDebugStringA(dbg.c_str());
            }
        } else {
            segment += *p++;
        }
    }
    flush();

    // Advance ImGui cursor to end of this row
    float totalH = (currentY - startY) + lineHeight + 4.f;
    ImGui::Dummy(ImVec2(availW, totalH));
}

void SAMPChatImGui::RenderChatWindow() {
    IDirect3DDevice9* pDev = samp_utils::GetGameDevice();
    if (pDev) {
        if (ProcessTextureTasks(pDev)) {
            m_scrollToBot = true;
        }
    }

    // Hide chat when ESC/pause menu is open
    if (samp_utils::IsMenuVisible()) {
        if (m_inputOpen) OnInputClose();
        m_alpha = fade.minAlpha;
        m_fadeState = FadeState::Idle;
        return;
    }

    if (m_alpha < 0.01f && !m_inputOpen) return;

    ImGuiIO& io = ImGui::GetIO();

    if (!m_layoutInitialized) {
        float scrW = io.DisplaySize.x;
        float scrH = io.DisplaySize.y;
        float winW = scrW * (widthPct / 100.f);
        float winH = scrH * (heightPct / 100.f);
        ImGui::SetNextWindowPos(ImVec2(windowX, windowY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
        m_layoutInitialized = true;
    }

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoBackground;

    if (!allowResize) {
        flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    }

    ImGui::PushFont(m_chatFont);

    ImGui::Begin("##SAMPChat", nullptr, flags);

    m_chatPos  = ImGui::GetWindowPos();
    m_chatSize = ImGui::GetWindowSize();

    // Chat lines child - force transparent background, no scrollbar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::BeginChild("##ChatLines", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);

    // Render messages
    int rowIdx = 0;
    for (const auto& line : m_lines) {
        RenderMessageRow(line, rowIdx++);
        ImGui::Dummy(ImVec2(0.f, 4.f)); // extra spacing between messages
    }

    if (m_scrollDelta != 0) {
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetScrollY(ImGui::GetScrollY() - m_scrollDelta * lineH);
        m_scrollDelta = 0;
    }

    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float lineH = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(ImGui::GetScrollY() - wheel * 3.f * lineH);
        }
    }

    if (m_scrollToBot) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBot = false;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg

    ImGui::End();
    ImGui::PopFont();
}

static void DrawGearIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col) {
    float r = size * 0.5f;
    int teeth = 8;
    float innerR = r * 0.55f;
    float outerR = r * 0.85f;
    for (int i = 0; i < teeth * 2; ++i) {
        float angle = (float)i / (teeth * 2) * 2.f * 3.14159f - 3.14159f / 2.f;
        float rr = (i % 2 == 0) ? outerR : innerR;
        ImVec2 p(center.x + cosf(angle) * rr, center.y + sinf(angle) * rr);
        if (i == 0) dl->PathLineTo(p);
        else dl->PathLineTo(p);
    }
    dl->PathFillConvex(col);
    dl->AddCircleFilled(center, innerR * 0.45f, IM_COL32(30, 30, 35, 255), 12);
}

static void DrawSendIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col) {
    float s = size * 0.5f;
    // Up arrow line and head
    dl->AddLine(ImVec2(center.x, center.y + s * 0.5f), ImVec2(center.x, center.y - s * 0.6f), col, 2.0f);
    dl->AddLine(ImVec2(center.x, center.y - s * 0.6f), ImVec2(center.x - s * 0.4f, center.y - s * 0.1f), col, 2.0f);
    dl->AddLine(ImVec2(center.x, center.y - s * 0.6f), ImVec2(center.x + s * 0.4f, center.y - s * 0.1f), col, 2.0f);
}

void SAMPChatImGui::AddToOwnHistory(const std::string& cmd) {
    if (cmd.empty()) return;
    // Don't add duplicates at the end
    if (!m_ownHistory.empty() && m_ownHistory.back() == cmd) return;
    m_ownHistory.push_back(cmd);
    if ((int)m_ownHistory.size() > MAX_OWN_HISTORY)
        m_ownHistory.erase(m_ownHistory.begin());
}

void SAMPChatImGui::RenderInputWindow() {
    if (!m_inputOpen) return;

    ImVec2 pos  = m_chatPos;
    ImVec2 size = m_chatSize;

    if (!m_layoutInitialized) {
        ImGuiIO& io = ImGui::GetIO();
        float scrW = io.DisplaySize.x;
        float scrH = io.DisplaySize.y;
        size = ImVec2(scrW * (widthPct / 100.f), scrH * (heightPct / 100.f));
        pos  = ImVec2(windowX, windowY);
    }

    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + size.y + 2.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size.x, 0.f), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8.f, 4.f));
    ImGui::PushFont(m_chatFont);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, m_alpha * 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.f, 0.f, 0.f, 0.f));

    ImGui::Begin("##SAMPInput", nullptr, flags);

    // Row: [Input text         ] [Send]
    float availW = ImGui::GetContentRegionAvail().x;
    float btnSize = 28.f;
    float inputW = availW - btnSize - 8.f;


    // Text input
    ImGui::PushItemWidth(inputW);
    if (m_focusInput) {
        ImGui::SetKeyboardFocusHere();
        m_focusInput = false;
    }

    auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        auto& self = SAMPChatImGui::Get();
        int n = (int)self.m_ownHistory.size();
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (n <= 0) return 0;
            if (self.m_nHistoryIndex == -1) {
                strncpy(self.m_szSavedInput, data->Buf, sizeof(self.m_szSavedInput) - 1);
                self.m_nHistoryIndex = n - 1;
            } else if (self.m_nHistoryIndex > 0) {
                self.m_nHistoryIndex--;
            }
            if (self.m_nHistoryIndex >= 0 && self.m_nHistoryIndex < n) {
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, self.m_ownHistory[self.m_nHistoryIndex].c_str());
            }
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (self.m_nHistoryIndex == -1) return 0;
            self.m_nHistoryIndex++;
            if (self.m_nHistoryIndex >= n) {
                self.m_nHistoryIndex = -1;
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, self.m_szSavedInput);
            } else {
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, self.m_ownHistory[self.m_nHistoryIndex].c_str());
            }
        }
        return 0;
    };

    ImGuiInputTextFlags inputFlags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackHistory;

    bool sent = false;
    const char* hintText = "\xe0\xb8\x9b\xe0\xb9\x89\xe0\xb8\xad\xe0\xb8\x99\xe0\xb8\x82\xe0\xb9\x89\xe0\xb8\xad\xe0\xb8\x84\xe0\xb8\xa7\xe0\xb8\xb2\xe0\xb8\xa1 \xe0\xb8\xab\xe0\xb8\xa3\xe0\xb8\xb7\xe0\xb8\xad\xe0\xb8\x9e\xe0\xb8\xb4\xe0\xb8\xa1\xe0\xb8\x9e\xe0\xb9\x8c / \xe0\xb9\x80\xe0\xb8\x9e\xe0\xb8\xb7\xe0\xb9\x88\xe0\xb8\xad\xe0\xb9\x83\xe0\xb8\x8a\xe0\xb9\x89\xe0\xb8\x84\xe0\xb8\xb3\xe0\xb8\xaa\xe0\xb8\xb1\xe0\xb9\x88\xe0\xb8\x87...";
    if (ImGui::InputTextWithHint("##ChatInput", hintText,
                                 m_inputBuf, sizeof(m_inputBuf),
                                 inputFlags, callback))
    {
        sent = true;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine(0.f, 8.f);

    // Send button
    ImVec2 sendPos = ImGui::GetCursorScreenPos();
    bool sendHovered = false;
    {
        ImVec2 cp = ImGui::GetCursorPos();
        ImGui::InvisibleButton("##Send", ImVec2(btnSize, btnSize));
        sendHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) sent = true;
        ImU32 sendBgCol = sendHovered
            ? IM_COL32(90, 90, 95, (int)(m_alpha * 255))
            : IM_COL32(70, 70, 75, (int)(m_alpha * 255));
        ImU32 arrowCol = sendHovered ? IM_COL32(255, 255, 255, (int)(m_alpha * 255)) : IM_COL32(200, 200, 200, (int)(m_alpha * 255));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        
        dl->AddRectFilled(
            ImVec2(sendPos.x, sendPos.y),
            ImVec2(sendPos.x + btnSize, sendPos.y + btnSize),
            sendBgCol,
            8.0f
        );
        DrawSendIcon(dl, ImVec2(sendPos.x + btnSize * 0.5f, sendPos.y + btnSize * 0.5f), btnSize, arrowCol);
        ImGui::SetCursorPos(cp);
        ImGui::Dummy(ImVec2(btnSize, btnSize));
    }

    // Command history row (own history, pills)
    if (!m_ownHistory.empty()) {
        ImGui::Spacing();
        for (size_t i = 0; i < m_ownHistory.size(); ++i) {
            if (i > 0) ImGui::SameLine(0.f, 4.f);
            const std::string& cmd = m_ownHistory[i];
            std::string label = cmd;
            if (label.length() > 14) label = label.substr(0, 11) + "...";
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.14f, 0.16f, m_alpha * 0.70f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.15f, 0.15f, m_alpha * 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.20f, 0.20f, m_alpha * 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.75f, 0.78f, 0.82f, m_alpha));
            if (ImGui::SmallButton(label.c_str())) {
                strncpy(m_inputBuf, cmd.c_str(), sizeof(m_inputBuf) - 1);
                m_inputBuf[sizeof(m_inputBuf) - 1] = '\0';
                m_focusInput = true;
            }
            ImGui::PopStyleColor(4);
            ImGui::PopID();
        }
    }
    
    if (sent && m_inputBuf[0] != '\0') {
        std::string utf8Text = m_inputBuf;
        std::string tisText = tis620::from_utf8(m_inputBuf);
        if (tisText[0] == '/') {
            AddToOwnHistory(utf8Text);
            if (tisText == "/setting" || tisText == "/settings") {
                m_showSettings = !m_showSettings;
                m_inputBuf[0] = '\0';
                m_focusInput = true;
                goto skip_close;
            } else if (tisText == "/q" || tisText == "/quit") {
                DISPATCH_COMMAND(Quit, "");
            } else if (tisText.rfind("/pagesize", 0) == 0) {
                DISPATCH_COMMAND(SetChatPageSize, tisText.c_str() + 10);
            } else if (tisText.rfind("/fontsize", 0) == 0) {
                DISPATCH_COMMAND(SetChatFontSize, tisText.c_str() + 10);
            } else if (tisText == "/timestamp") {
                DISPATCH_COMMAND(DrawChatTimestamps, "");
            } else if (tisText == "/headmove") {
                DISPATCH_COMMAND(ToggleHeadMoves, "");
            } else if (tisText.rfind("/fpslimit", 0) == 0) {
                DISPATCH_COMMAND(SetFrameLimiter, tisText.c_str() + 10);
            } else if (tisText == "/savepos") {
                DISPATCH_COMMAND(SavePosition, "");
            } else if (tisText == "/interior") {
                DISPATCH_COMMAND(PrintCurrentInterior, "");
            } else if (tisText == "/debug") {
                DISPATCH_COMMAND(ToggleDebugLabels, "");
            } else if (tisText == "/audiomsg") {
                DISPATCH_COMMAND(ToggleAudioStreamMessages, "");
            } else if (tisText == "/nametagstatus") {
                DISPATCH_COMMAND(DrawNameTagStatus, "");
            } else if (tisText.rfind("/rcon", 0) == 0) {
                DISPATCH_COMMAND(SendRconCommand, tisText.c_str() + 6);
            } else if (tisText == "/saveposonly") {
                DISPATCH_COMMAND(SavePositionOnly, "");
            } else if (tisText == "/camera") {
                DISPATCH_COMMAND(ToggleCameraTargetLabels, "");
            } else {
                samp_utils::SendCommand(tisText);
            }
        } else {
            samp_utils::SendChat(tisText);
            AddToOwnHistory(utf8Text);
        }

        m_inputBuf[0] = '\0';
        m_scrollToBot = true;
        OnInputClose();
        samp_utils::CloseInputBox();
    skip_close:;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        samp_utils::CloseInputBox();
    }

    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    ImGui::PopStyleVar(3);
}

void SAMPChatImGui::RenderSettingsWindow() {
    if (!m_showSettings) return;

    if (!m_layoutInitialized) return;

    // Position at the far right side of the screen, vertically centered
    float screenW = 0.f, screenH = 0.f;
    if (m_hwnd) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        screenW = (float)(rc.right - rc.left);
        screenH = (float)(rc.bottom - rc.top);
    } else {
        ImGuiIO& io = ImGui::GetIO();
        screenW = io.DisplaySize.x;
        screenH = io.DisplaySize.y;
    }
    float settingsW = 320.f;
    float margin = 10.f;
    // Right-aligned: anchor pivot (1.0, 0.5) at (screenW - margin, screenH * 0.5)
    ImGui::SetNextWindowPos(ImVec2(screenW - margin, screenH * 0.5f), ImGuiCond_Always, ImVec2(1.0f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(settingsW, 0.f), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 12.f));
    ImGui::PushFont(m_chatFont);

    if (ImGui::Begin("##ChatSettings", &m_showSettings, flags)) {
        ImGui::TextColored(ImVec4(0.90f, 0.25f, 0.25f, 1.f), "Chat Settings");
        ImGui::Separator();

        // Timestamp checkbox — syncs with sa-mp.cfg
        bool prevTimestamp = showTimestamp;
        ImGui::Checkbox("Show Timestamp", &showTimestamp);
        if (showTimestamp != prevTimestamp) {
            WriteSampCfgInt("timestamp", showTimestamp ? 1 : 0);
        }

        ImGui::Checkbox("Allow Resize", &allowResize);

        ImGui::SliderFloat("Opacity", &m_chatOpacity, 0.2f, 1.0f);
        ImGui::SliderFloat("Rounding", &m_rounding, 0.f, 16.f);

        ImGui::Separator();

        if (ImGui::Button("Reset Layout", ImVec2(-1, 0))) {
            m_layoutInitialized = false;
        }
        if (ImGui::Button("Close", ImVec2(-1, 0))) {
            m_showSettings = false;
        }
    }
    ImGui::End();

    ImGui::PopFont();
    ImGui::PopStyleVar(2);
}

void SAMPChatImGui::Tick(IDirect3DDevice9* /*device*/) {
    if (!m_initialized) return;

    DWORD64 now = GetTickCount64();
    float   dt  = (now - m_lastTick) / 1000.f;
    m_lastTick  = now;
    if (dt > 0.1f) dt = 0.1f;

    UpdateFade(dt);

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderChatWindow();
    RenderInputWindow();
    RenderSettingsWindow();

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void SAMPChatImGui::SetFont(const std::string& path, float size) {
    customFontPath = path;
    fontSize = size;
    if (m_initialized) {
        BuildFonts();
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
}