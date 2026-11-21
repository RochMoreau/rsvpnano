// Companion sync on NextUI. Replaces src/companion/http/CompanionApi.cpp's start-up and shutdown:
// NextUI owns Wi-Fi, so instead of an ESP32 access point the companion API is served on the
// network the Brick is already connected to. Every API route is the firmware's own
// (CompanionHttp.cpp and the *Api.cpp files), served by ports/nextui/runtime/HttpServer.cpp.
//
// The Brick also serves a small upload page at "/" so books can be sent from any browser on the
// same network. Unlike the API, that page accepts EPUB and PDF files as they are: the Brick
// converts them itself when they are opened.
#include "companion/http/CompanionApi.h"

#include <FS.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <glaze/net/url.hpp>


#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

#include "board/BoardConfig.h"
#include "board/BoardStorage.h"
#include "companion/http/CompanionUpload.h"
#include "logging/Logger.h"
#include "platforms/nextui/NextUiHost.h"
#include "platforms/nextui/NextUiNetwork.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "update/OtaUpdater.h"

namespace {

    constexpr const char* kTag = "companion";
    constexpr size_t kMaxRawUploadBytes = 512UL * 1024UL * 1024UL;

    // What the upload page needs from the companion, captured in CompanionApi::begin().
    struct UploadContext {
        StorageManager* storage = nullptr;
        screens::LibraryScreen* library = nullptr;
        std::mutex* operations = nullptr;
        const std::atomic_bool* active = nullptr;
    };
    UploadContext gUpload;

    esp_err_t sendText(httpd_req_t* request, const char* status, const char* type, std::string_view body) {
        httpd_resp_set_status(request, status);
        httpd_resp_set_type(request, type);
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
    }

    std::string jsonString(std::string_view text) {
        std::string out = "\"";
        for (const char c: text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out += c;
            }
        }
        return out + "\"";
    }

    esp_err_t sendUploadError(httpd_req_t* request, const char* status, std::string_view message, bool close) {
        if (close)
            httpd_resp_set_hdr(request, "Connection", "close");
        sendText(request, status, "application/json", "{\"message\":" + jsonString(message) + "}");
        return close ? ESP_FAIL : ESP_OK;
    }

    std::string statusLine(t_http_codes status) {
        return companion::api::httpStatusLine(status);
    }

    // POST /upload?name=FILE — the raw file as the body.
    esp_err_t handleUpload(httpd_req_t* request) {
        if (gUpload.active == nullptr || !gUpload.active->load())
            return ESP_FAIL;

        const size_t queryLength = httpd_req_get_url_query_len(request);
        std::string query(queryLength + 1, '\0');
        std::string encodedName(queryLength + 1, '\0');
        if (queryLength == 0 || httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK
            || httpd_query_key_value(query.c_str(), "name", encodedName.data(), encodedName.size()) != ESP_OK) {
            return sendUploadError(request, "400 Bad Request", "The file name is missing", true);
        }
        encodedName.resize(encodedName.find('\0'));
        const std::string filename = StoragePaths::sanitizeFilename(glz::url_decode(encodedName));
        if (filename.empty())
            return sendUploadError(request, "400 Bad Request", "The file name is not valid", true);
        if (!StoragePaths::hasRsvpExtension(filename) && !StoragePaths::hasTextExtension(filename)
            && !StoragePaths::hasConvertibleDocumentExtension(filename)) {
            return sendUploadError(request, "415 Unsupported Media Type",
                                   "Send EPUB, PDF, RSVP, text, Markdown or HTML files", true);
        }
        if (request->content_len == 0)
            return sendUploadError(request, "400 Bad Request", "The file is empty", true);

        if (auto created = StorageFiles::ensureDirectory(StoragePaths::kBookFilesPath); !created)
            return sendUploadError(request, "500 Internal Server Error", "The library folder is not available", true);

        const std::string finalPath = std::string{StoragePaths::kBookFilesPath} + "/" + filename;
        auto upload = companion::TemporaryUpload::receive(*request, Board::Storage::filesystem(), finalPath + ".tmp",
                                                          kMaxRawUploadBytes, "Book");
        if (!upload) {
            const auto& error = upload.error();
            return sendUploadError(request, statusLine(error.status).c_str(), error.error.message,
                                   error.connection == companion::api::ConnectionPolicy::Close);
        }

        {
            const std::lock_guard lock{*gUpload.operations};
            if (auto installed = gUpload.storage->installBook(upload->path(), finalPath); !installed) {
                Logger::failure(kTag, "install upload", finalPath.c_str(), installed.error());
                return sendUploadError(request, "500 Internal Server Error", "The book could not be saved", false);
            }
            gUpload.library->invalidate();
        }
        ESP_LOGI(kTag, "uploaded %s (%u bytes)", finalPath.c_str(), static_cast<unsigned>(request->content_len));
        return sendText(request, "201 Created", "application/json", "{\"name\":" + jsonString(filename) + "}");
    }

    constexpr std::string_view kUploadPage = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RSVP Nano</title><link rel="icon" href="data:,">
<style>
:root{color-scheme:dark;--bg:#0b0b0b;--panel:#1c1c1c;--line:#333;--text:#eee;--muted:#999;--accent:#e62828}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:24px 16px 48px}h1{font-size:22px;margin:0 0 4px}
h1 b{color:var(--accent)}p.sub{color:var(--muted);margin:0 0 20px}
#drop{display:block;border:2px dashed var(--line);border-radius:12px;padding:28px 16px;text-align:center;cursor:pointer;background:var(--panel)}
#drop.over{border-color:var(--accent)}#drop strong{display:block;font-size:18px;margin-bottom:4px}#drop span{color:var(--muted);font-size:14px}
input[type=file]{display:none}#queue{margin-top:8px}h2{font-size:15px;text-transform:uppercase;letter-spacing:.06em;color:var(--accent);margin:28px 0 8px}
ul{list-style:none;padding:0;margin:0}li{display:flex;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid var(--line)}
li .name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}li .meta{color:var(--muted);font-size:13px}
li progress{width:96px;accent-color:var(--accent)}li .state{font-size:13px;color:var(--muted);min-width:64px;text-align:right}
li .state.err{color:var(--accent)}button{background:none;border:1px solid var(--line);color:var(--muted);border-radius:8px;padding:4px 10px;font:inherit;font-size:13px;cursor:pointer}
button:hover{color:var(--text);border-color:var(--muted)}.empty{color:var(--muted);padding:10px 0}
</style></head><body><main>
<h1>RSVP <b>Nano</b></h1>
<p class="sub">Send books to your Brick. EPUB and PDF files are converted on the device when you open them.</p>
<label id="drop"><strong>Choose books</strong><span>or drop them here · EPUB, PDF, RSVP, TXT, MD, HTML</span>
<input id="file" type="file" multiple accept=".epub,.pdf,.rsvp,.txt,.md,.markdown,.html,.htm,.xhtml"></label>
<ul id="queue"></ul>
<h2>On the Brick</h2><ul id="books"><li class="empty">Loading…</li></ul>
</main><script>
const $=s=>document.querySelector(s);
const size=b=>b>1048576?(b/1048576).toFixed(1)+' MB':Math.max(1,Math.round(b/1024))+' KB';
function row(name,meta){const li=document.createElement('li');li.innerHTML='<div class="name"></div><div class="meta"></div>';
li.querySelector('.name').textContent=name;li.querySelector('.meta').textContent=meta||'';return li}
async function loadBooks(){const ul=$('#books');try{const r=await fetch('/api/v2/library');if(!r.ok)throw 0;const books=await r.json();
ul.innerHTML='';if(!books.length){ul.innerHTML='<li class="empty">No books yet</li>';return}
books.sort((a,b)=>(a.metadata?.title||a.name).localeCompare(b.metadata?.title||b.name));
for(const b of books){const li=row(b.metadata?.title||b.name,size(b.bytes));const del=document.createElement('button');del.textContent='Delete';
del.onclick=async()=>{if(!confirm('Delete “'+(b.metadata?.title||b.name)+'”?'))return;
let r=await fetch('/api/v2/library/'+encodeURIComponent(b.id),{method:'DELETE'});
if(r.status==409&&confirm('This book is open on the Brick. Delete anyway?'))r=await fetch('/api/v2/library/'+encodeURIComponent(b.id)+'?force=true',{method:'DELETE'});
loadBooks()};li.append(del);ul.append(li)}}catch(e){ul.innerHTML='<li class="empty">Could not reach the Brick. Is Sync still open?</li>'}}
function upload(file){return new Promise(done=>{const li=row(file.name,size(file.size));const bar=document.createElement('progress');bar.max=1;bar.value=0;
const state=document.createElement('div');state.className='state';state.textContent='Waiting';li.append(bar,state);$('#queue').append(li);
const x=new XMLHttpRequest();x.open('POST','/upload?name='+encodeURIComponent(file.name));
x.upload.onprogress=e=>{if(e.lengthComputable){bar.value=e.loaded/e.total;state.textContent=Math.round(100*e.loaded/e.total)+'%'}};
x.onload=()=>{bar.remove();if(x.status==201){state.textContent='Sent';}else{state.classList.add('err');
try{state.textContent=JSON.parse(x.responseText).message}catch(e){state.textContent='Failed ('+x.status+')'}}done()};
x.onerror=()=>{bar.remove();state.classList.add('err');state.textContent='Connection lost';done()};
state.textContent='Sending';x.send(file)})}
async function send(files){for(const f of files)await upload(f);loadBooks()}
$('#file').onchange=e=>{send([...e.target.files]);e.target.value=''};
const drop=$('#drop');['dragenter','dragover'].forEach(t=>drop.addEventListener(t,e=>{e.preventDefault();drop.classList.add('over')}));
['dragleave','drop'].forEach(t=>drop.addEventListener(t,e=>{e.preventDefault();drop.classList.remove('over')}));
drop.addEventListener('drop',e=>send([...e.dataTransfer.files]));loadBooks();
</script></body></html>
)HTML";

    esp_err_t handleUploadPage(httpd_req_t* request) {
        if (gUpload.active == nullptr || !gUpload.active->load())
            return ESP_FAIL;
        return sendText(request, "200 OK", "text/html; charset=utf-8", kUploadPage);
    }

    std::string serverUrl(const std::string& address, uint16_t port) {
        std::string url = "http://" + address;
        if (port != 80)
            url += ":" + std::to_string(port);
        return url;
    }

} // namespace

bool CompanionApi::begin() {
    if (active())
        return true;

    Logger::checkpoint("companion_start");
    stationConnected_.store(false);

    const std::string address = NextUiNetwork::address();
    if (address.empty()) {
        statusLine1_ = "No Wi-Fi connection";
        statusLine2_ = "Connect in NextUI settings";
        return false;
    }

    readerScreen_.releaseRuntimeCaches();
    gUpload = {.storage = &storage_, .library = &libraryScreen_, .operations = &operationsMutex_, .active = &active_};
    if (auto started = startServer(); !started) {
        statusLine1_ = "Server failed";
        statusLine2_ = started.error();
        return false;
    }

    httpd_uri_t page{};
    page.uri = "/";
    page.method = HTTP_GET;
    page.handler = handleUploadPage;
    httpd_uri_t upload{};
    upload.uri = "/upload";
    upload.method = HTTP_POST;
    upload.handler = handleUpload;
    if (httpd_register_uri_handler(server_, &page) != ESP_OK || httpd_register_uri_handler(server_, &upload) != ESP_OK)
        ESP_LOGW(kTag, "upload page unavailable");

    accessPointSsid_ = "RSVP-Nano-" + deviceSuffix();
    stationUrl_ = serverUrl(address, nextui_httpd_port(server_));
    statusLine1_ = "Scan, or open in a browser:";
    statusLine2_ = stationUrl_;
    NextUiHost::setQrCode(stationUrl_);
    active_.store(true);
    ESP_LOGI(kTag, "ready url=%s", stationUrl_.c_str());
    Logger::checkpoint("companion_sync");
    return true;
}

void CompanionApi::end() {
    Logger::checkpoint("companion_stop");
    active_.store(false);
    stationConnected_.store(false);
    NextUiHost::setQrCode({});
    // Unlike the ESP32 (which keeps its httpd task for the firmware's lifetime), close the port
    // whenever Sync is not open. httpd_stop waits for an in-flight request to finish.
    stopServer();
    {
        const std::lock_guard lock{operationsMutex_};
        storage_.refreshBooks();
        libraryScreen_.invalidate();
    }
    readerScreen_.refreshTypography();

    accessPointSsid_.clear();
    stationUrl_.clear();
    statusLine1_ = "Idle";
    statusLine2_.clear();
    Logger::checkpoint("running");
}

bool CompanionApi::active() const {
    return active_.load();
}

void CompanionApi::renderStatus(bool usbConnected) {
    const std::scoped_lock lock{operationsMutex_, networkStateMutex_};
    if (usbConnected)
        screens::status(ui_, "USB companion", "Connected", "Keep the browser open");
    else
        screens::status(ui_, ui_.text(UiText::Sync), statusLine1(), statusLine2());
}

std::string_view CompanionApi::statusLine1() const {
    return statusLine1_;
}

std::string_view CompanionApi::statusLine2() const {
    return statusLine2_;
}

companion::api::Result<companion::api::DeviceInfo> CompanionApi::getDevice(httpd_req_t& request) {
    (void) request;
    return deviceInfo();
}

companion::api::DeviceInfo CompanionApi::deviceInfo() const {
    return companion::api::DeviceInfo{
        .ssid = accessPointSsid_.empty() ? "RSVP-Nano-" + deviceSuffix() : accessPointSsid_,
        .firmwareVersion = std::string{OtaUpdater::currentVersion()},
        .otaAsset = Board::Config::OTA_ASSET_NAME,
    };
}

companion::api::Result<StorageMigration::Report> CompanionApi::repairStorage(httpd_req_t& request) {
    (void) request;
    auto report = StorageMigration::repair(storage_.mounted(), {
                                                                   .libraryItems = storage_.books().size(),
                                                                   .fonts = readerScreen_.fonts.families().size() - 1,
                                                                   .themes = interfaceScreen_.themes.themes().size() - 1,
                                                               });
    if (storage_.mounted()) {
        storage_.refreshBooks();
        readerScreen_.fonts.loadFromSd();
        interfaceScreen_.themes.loadFromSd();
        localeCatalog_ = locales::scanInstalled(Board::Storage::filesystem(), static_cast<size_t>(UiText::Count));
    }
    return report;
}

std::string CompanionApi::deviceSuffix() const {
    const uint64_t mac = ESP.getEfuseMac();
    char suffix[7]{};
    std::snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(mac & 0xFFFFFF));
    return suffix;
}
