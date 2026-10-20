#include "protocol/viewer/native/viewer_interface.h"
#include "protocol/viewer/native/viewer_settings.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "gw_fonts.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <iomanip>
#include <sstream>

static GwViewer::State view;
static std::unique_ptr<Manager::GwPlot> ui;
static bool applyingState = false;
static std::string acceptedSettings;
static std::string acceptedTheme;
static GLFWwindow* window = nullptr;
static GLuint texture = 0, commandTexture = 0, composeTexture = 0, composeFbo = 0;
static int composeW=0, composeH=0;
static sk_sp<SkSurface> commandSurface;
static std::vector<unsigned char> commandPixels;
static std::string commandStamp;
static void drawLocalCommandBox() {
    if (!ui->commandBoxOpen()) return;
    std::string stamp=ui->commandBoxState() + "|" + std::to_string(ui->fb_width) + "x" + std::to_string(ui->fb_height) + "|" + view.theme + "|" + std::to_string(ui->opts.font_size);
    if (stamp != commandStamp) {
        const int w=ui->fb_width,h=ui->fb_height;
        if (!commandSurface || commandSurface->width() != w || commandSurface->height() != h)
            commandSurface=SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w,h));
        commandSurface->getCanvas()->clear(SK_ColorTRANSPARENT);
        ui->drawCommandBox(commandSurface->getCanvas());
        commandPixels.resize(size_t(w)*h*4);
        commandSurface->readPixels(SkImageInfo::Make(w,h,kRGBA_8888_SkColorType,kUnpremul_SkAlphaType),commandPixels.data(),size_t(w)*4,0,0);
        GLint binding; glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        glBindTexture(GL_TEXTURE_2D,commandTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,commandPixels.data());
        glBindTexture(GL_TEXTURE_2D,binding);
        commandStamp=std::move(stamp);
    }
    ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(commandTexture),ImVec2(0,0),ImGui::GetIO().DisplaySize);
}
static bool busy = true, hasImage = false;
static std::string error;
static float remoteTrackY=0, remoteGap=0, remoteRefSpace=0, remoteSliderSpace=0;

extern "C" {
EMSCRIPTEN_KEEPALIVE void remote_files(const char* names) {
    if(!ui) return;
    ui->bam_paths.clear(); std::istringstream input(names);std::string name;
    while(std::getline(input,name)) if(!name.empty()) ui->bam_paths.push_back(name);
}
EMSCRIPTEN_KEEPALIVE void remote_state(int rev, int width, int height, int selection, const char* region, const char* theme, const char* settings, const char* popups, const char* output, const char* scrollbars, const char* tracks) {
    view.revision = rev; view.width = width; view.height = height;
    view.region = region; view.theme = theme; view.settings = settings;
    if (!ui) return;
    applyingState = true;
    if (output && *output) { ui->lastCommandOutputAnsi += output; ++ui->lastCommandOutputFrame; ui->showCommandStatus = true; }
    std::istringstream popupInput(popups);
    std::string kind;
    while (popupInput >> kind) {
        int id; float x,y; std::string ansi;
        if (!(popupInput >> id >> x >> y >> std::quoted(ansi))) break;
        if (kind == "read") {
            Manager::GwPlot::ReadPopup v; v.uid=id; v.x=x; v.y=y; v.ansi=ansi;
            popupInput >> std::quoted(v.sam) >> std::quoted(v.qname);
            ui->readPopups.push_back(std::move(v));
        } else if (kind == "cov") {
            Manager::GwPlot::CovPopup v; v.uid=id; v.x=x; v.y=y; v.ansi=ansi;
            popupInput >> std::quoted(v.chromPos); ui->covPopups.push_back(std::move(v));
        } else if (kind == "track") {
            Manager::GwPlot::TrackPopup v; v.uid=id; v.x=x; v.y=y; v.ansi=ansi;
            popupInput >> std::quoted(v.details) >> v.isVcf; ui->trackPopups.push_back(std::move(v));
        } else if (kind == "seq") {
            Manager::GwPlot::SeqPopup v; v.uid=id; v.x=x; v.y=y; v.ansi=ansi;
            int k; popupInput >> k >> v.aaFrame >> v.aaStrand >> v.aaCode;
            v.kind=static_cast<Manager::GwPlot::SeqPopup::Kind>(k); ui->seqPopups.push_back(std::move(v));
        } else break;
    }
    if (ui->opts.theme_str != theme) {
        std::string name = theme;
        ui->opts.setTheme(name);
        Themes::applyImGuiTheme(name);
    }
    auto& style=ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg].w=1; style.Colors[ImGuiCol_PopupBg].w=1;
    GwViewer::applyRenderSettings(ui->opts, settings);
    ui->opts.settings_save_pending = false;
    ui->regions.clear();
    std::istringstream regionInput(region); std::string input;
    while (std::getline(regionInput,input)) if (!input.empty()) ui->regions.push_back(Utils::parseRegion(input));
    ui->regionSelection=ui->regions.empty()?0:std::clamp(selection,0,int(ui->regions.size())-1);
    ui->fonts.setTypeface(ui->opts.font_str, ui->opts.font_size);
    ui->fonts.setOverlayHeight(1);
    // Reflect actual state in the existing settings widgets.
    std::istringstream lines(settings);
    std::string key; double value;
    while (lines >> key >> value) {
        std::string name = key;
        std::string section = "general";
        if (key == "snp_threshold") { section = "view_thresholds"; name = "snp"; }
        if (key == "small_indel_threshold") { section = "view_thresholds"; name = "small_indel"; }
        if (key == "soft_clip_threshold") { section = "view_thresholds"; name = "soft_clip"; }
        if (key == "mod_threshold") { section = "view_thresholds"; name = "mod"; }
        if (key == "edge_highlights") section = "view_thresholds";
        if (key == "tab_track_height") name = "tabix_track_height";
        if (key == "parse_mods") name = "mods";
        if (key == "max_coverage") { name = "coverage"; value = value > 0; }
        if (section == "view_thresholds" && value <= 0) continue; // Off is not the saved on-threshold.
        if (key == "link_op") {
            ui->opts.myIni[section]["link"] = value == 0 ? "none" : value == 1 ? "sv" : "all";
        } else {
            const bool boolean = name == "coverage" || name == "mods" || name == "scale_bar" ||
                name == "log2_cov" || name == "expand_tracks";
            ui->opts.myIni[section][name] = boolean ? (value ? "true" : "false") : std::to_string(value);
        }
    }
    ui->remoteTracks.clear();
    std::istringstream trackInput(tracks); Manager::GwPlot::RemoteTrack track;
    while (trackInput>>track.top>>track.height>>std::quoted(track.name)) ui->remoteTracks.push_back(track);
    ui->remoteScrollbars.clear();
    std::istringstream barInput(scrollbars);
    bool serverLine=false; // Cursor guides are a client-local display preference.
    barInput >> remoteTrackY >> remoteGap >> serverLine >> remoteRefSpace >> remoteSliderSpace;
    Manager::GwPlot::RemoteScrollbar bar;
    while (barInput >> bar.xOffset >> bar.yOffset >> bar.regionPixels >> bar.maximum >> bar.value >> bar.live)
        ui->remoteScrollbars.push_back(bar);
    acceptedSettings = GwViewer::renderSettings(ui->opts);
    acceptedTheme = ui->opts.theme_str;
    applyingState = false;
}
EMSCRIPTEN_KEEPALIVE void remote_status(int loading, int image, const char* message) {
    busy = loading; if (image) hasImage = true;
    error = message ? message : "";
    if (!error.empty() && ui) {
        ui->lastCommandOutputAnsi = error;
        ++ui->lastCommandOutputFrame;
        ui->showCommandStatus = true;
    }
}
}

EM_JS(void, remoteConnect, (int textureId), {
    function textCall(name, args, strings) {
        var allocated = strings.map(s => stringToNewUTF8(s));
        try { Module[name](...args, ...allocated); }
        finally { allocated.forEach(p => _free(p)); }
    }
    var remote = Module.remote = {state: null, busy: false, texture: textureId, pendingSize: null, inputs: []};
    remote.status = (busy, image, message = "") => textCall('_remote_status', [busy ? 1 : 0, image ? 1 : 0], [message]);
    remote.present = async function(view, bytes) {
        if(view.frame.layout.encoding!=='gw-ui-text-1') throw new Error('Server does not support the GW UI codec');
        var frame = view.frame, state = Object.assign({}, view.shared, view.client, {
            revision:view.revision, width:frame.width, height:frame.height,
            pixel_ratio:frame.viewport.pixel_ratio,
            scrollbars:frame.layout.scrollbars || '', tracks:frame.layout.tracks || ''
        });
        state.settings = Object.entries(view.shared.settings).map(([k,v])=>k+' '+v).join('\n');
        remote.frameBlob = new Blob([bytes], {type:'image/png'});
        var bitmap = await createImageBitmap(remote.frameBlob);
        try {
            var binding = GLctx.getParameter(GLctx.TEXTURE_BINDING_2D);
            GLctx.bindTexture(GLctx.TEXTURE_2D, GL.textures[remote.texture]);
            GLctx.texImage2D(GLctx.TEXTURE_2D, 0, GLctx.RGBA, GLctx.RGBA, GLctx.UNSIGNED_BYTE, bitmap);
            GLctx.bindTexture(GLctx.TEXTURE_2D, binding);
        } finally { bitmap.close(); }
        var event = state.event_id || frame.frame_id;
        var popups = remote.popupEvent === event ? '' : (state.popups || '');
        remote.popupEvent = event;
        remote.state = state;
        // This integer is only an ImGui refresh counter; wire revisions stay strings.
        remote.uiRevision = (remote.uiRevision || 0) + 1;
        textCall('_remote_state', [remote.uiRevision, state.width, state.height, state.active_region || 0], [(state.regions || []).join("\n"), state.theme, state.settings, popups, remote.outputEvent !== event ? (state.output || "") : "", state.scrollbars, state.tracks]);
        textCall('_remote_files', [], [(state.alignments || []).join('\n')]);
        remote.outputEvent = event;
        remote.status(false, true);
        if(Module.viewerOnState) Module.viewerOnState(view);
    };
    remote.convert = function(action) {
        // These records are generated with spaces by the native callbacks.
        // Avoid backslash regex escapes inside EM_JS's C string representation.
        var ratio=remote.state.pixel_ratio, value=action.value || '', parts=value.trim().split(/ +/);
        var mods=n=>['shift','control','alt','meta'].filter((_,i)=>n & (1<<i));
        if(action.type==='input') return {type:'pointer',phase:parts[0],x:Number(parts[1])/ratio,y:Number(parts[2])/ratio,
            button:'left',modifiers:mods(Number(parts[4])),dx:Number(parts[5]),dy:Number(parts[6])};
        if(action.type==='settings') {
            var values={}; value.trim().split('\n').forEach(line=>{var p=line.trim().split(/ +/);if(p.length===2)values[p[0]]=Number(p[1]);});
            return {type:'settings',values};
        }
        var region=action.region_index>=0?{region_index:action.region_index}:{};
        if(action.type==='navigate') return {type:'navigate',region:value,...region};
        if(parts[0]==='vscroll') return {type:'vscroll',collection_id:parts[1],offset:Number(parts[2])};
        if(parts[0]==='track-resize') return {type:'track_resize',track_id:parts[1],y:Number(parts[2])/ratio};
        if(parts[0]==='key') {
            var keys={263:'ArrowLeft',262:'ArrowRight',265:'ArrowUp',264:'ArrowDown',266:'PageUp',267:'PageDown',91:'BracketLeft',93:'BracketRight',76:'KeyL',70:'KeyF',82:'KeyR',45:'Minus',61:'Equal'};
            return {type:'key',key:keys[parts[1]],modifiers:mods(Number(parts[2])),...region};
        }
        return {type:'command',text:value,...region};
    };
    remote.load = async function(action) {
        if (remote.busy) { if (action) remote.inputs.push(action); return; }
        if (!remote.client) return;
        remote.busy = true; remote.status(true, false);
        try {
            if(action && action.type==='resize') await remote.client.resize({width:Math.round(action.width/action.pixel_ratio),height:Math.round(action.height/action.pixel_ratio),pixel_ratio:action.pixel_ratio});
            else if(action) await remote.client.action(remote.convert(action));
            else await remote.client.snapshot();
        } catch(e) {
            remote.inputs = [];
            try {
                // Exact retry is safe even if the server applied the action before disconnecting.
                if(remote.client.pending) await remote.client.retry();
                if(e.code==='client_expired') await remote.reconnect();
                else await remote.client.snapshot();
                if(action && action.type==='input') await remote.client.action({type:'pointer',phase:'cancel',button:'left',x:0,y:0,modifiers:[]});
            } catch(_) {}
            remote.status(false,false,e.message);
        } finally {
            remote.busy = false;
            if(remote.pendingSize) {
                var size=remote.pendingSize;remote.pendingSize=null;
                remote.inputs=remote.inputs.filter(a=>a.type!=='input');remote.resize(size[0],size[1],size[2]);
            } else if(remote.inputs.length) remote.load(remote.inputs.shift());
            else if(remote.needsSnapshot) {remote.needsSnapshot=false;remote.load();}
        }
    };
    remote.resize = function(w,h,dpr) {
        if (!remote.state || remote.busy) { remote.pendingSize = [w,h,dpr]; return; }
        if (remote.state.width !== w || remote.state.height !== h || remote.state.pixel_ratio !== dpr)
            remote.load({type:'resize',width:w,height:h,pixel_ratio:dpr});
    };
    remote.input = function(value) {
        var action = {type:'input', value:value};
        var move = value.startsWith('move ');
        if (!remote.busy) remote.load(action);
        else {
            if (move && remote.inputs.length && remote.inputs[remote.inputs.length-1].type === "input" && remote.inputs[remote.inputs.length-1].value.startsWith('move ')) remote.inputs.pop();
            if (remote.inputs.length < 128) remote.inputs.push(action);
            else { remote.inputs = [{type:'input',value:'cancel 0 0 0 0 0 0'}]; }
        }
    };
    remote.reconnect=async function() {
        var canvas=Module.canvas, ratio=window.devicePixelRatio || 1;
        var viewport={width:canvas.clientWidth,height:canvas.clientHeight,pixel_ratio:ratio};
        var saved=remote.client.resumeCredentials;
        try { await remote.client.join(Module.viewerSession || 'default',viewport,saved); }
        catch(e) { if(e.code!=='client_expired') throw e; await remote.client.join(Module.viewerSession || 'default',viewport); }
    };
    Module.viewerProtocolReady.then(async function(api) {
        var transport=Module.viewerTransport || api.httpTransport({messageUrl:'/viewer/v1/message',frameUrl:'/viewer/v1/frame'});
        remote.client=new api.ViewerClient(transport,{onView:remote.present,onRevision:function() {
            if(remote.busy)remote.needsSnapshot=true;else remote.load();
        }});
        await remote.reconnect();
        if(Module.viewerOnReady) Module.viewerOnReady();
        // Heartbeat detects shared edits; unchanged heartbeats do not fetch PNGs.
        remote.heartbeat=setInterval(async function() {
            if(remote.busy)return;
            try {await remote.client.heartbeat();} catch(e) {remote.load();}
        },Math.min(1000,remote.client.heartbeatSeconds*1000));
        if(remote.pendingSize){var size=remote.pendingSize;remote.pendingSize=null;remote.resize(...size);}
    }).catch(e=>{remote.status(false,false,e.message);if(Module.viewerOnError)Module.viewerOnError(e.message);});
});
EM_JS(void, sendAction, (int type, const char* value, int width, int height, int regionIndex), {
    if (!Module.remote || !Module.remote.state) return;
    Module.remote.load({type:['navigate','command','resize','settings'][type],value:UTF8ToString(value),width:width,height:height,region_index:regionIndex});
});
EM_JS(void, requestSize, (int width, int height, double dpr), {
    clearTimeout(Module.resizeTimer);
    Module.resizeTimer = setTimeout(() => Module.remote.resize(width,height,dpr), 150);
});
EM_JS(void, sendPointer, (const char* value), {
    if (Module.remote && Module.remote.state) Module.remote.input(UTF8ToString(value));
});
static bool remoteDrag = false;
static void pointerInput() {
    auto& io=ImGui::GetIO();
    if (ui->commandBoxOpen()) {
        ui->mousePos(io.MousePos.x,io.MousePos.y);
        if (ImGui::IsMouseClicked(0)) ui->mouseButton(0,GLFW_PRESS,0);
        if (ImGui::IsMouseReleased(0)) { ui->mouseButton(0,GLFW_RELEASE,0); if (ui->commandBoxReady()) ui->commandProcessed(); }
        return;
    }
    const bool captured=io.WantCaptureMouse || ImGui::IsAnyItemActive();
    const float ratio=ui->monitorScale;
    const float x=std::clamp(io.MousePos.x*ratio,0.f,float(view.width));
    const float y=std::clamp(io.MousePos.y*ratio,0.f,float(view.height));
    const int mods=(io.KeyShift?1:0)|(io.KeyCtrl?2:0)|(io.KeyAlt?4:0)|(io.KeySuper?8:0);
    auto send=[&](const char* kind,int button=0,double dx=0,double dy=0) {
        std::ostringstream out; out << kind << ' ' << x << ' ' << y << ' ' << button << ' ' << mods << ' ' << dx << ' ' << dy;
        sendPointer(out.str().c_str());
    };
    if (remoteDrag && (!io.AppFocusLost && io.MousePos.x > -10000 && !ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        if (ImGui::IsMouseReleased(0)) { send("release"); ui->endScaleBarSelection(); remoteDrag=false; }
        else if (!ui->scaleBarSelectionActive() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) send("move");
    } else if (remoteDrag) { send("cancel"); ui->endScaleBarSelection(); remoteDrag=false; }
    if (!captured && !remoteDrag && ui->fb_width == view.width && ui->fb_height == view.height) {
        if (!busy && ImGui::IsMouseClicked(0)) { ui->beginScaleBarSelection(x,y); send("press"); remoteDrag=true; }
        // A discrete wheel step must cause one native zoom, even when the
        // browser/GLFW backend expresses that step as a fractional delta.
        if (io.MouseWheel != 0 || io.MouseWheelH != 0)
            send("wheel",0,io.MouseWheelH == 0 ? 0 : std::copysign(1.0,io.MouseWheelH),
                         io.MouseWheel == 0 ? 0 : std::copysign(1.0,io.MouseWheel));
    }
}
EM_JS(void, downloadFrame, (const char* filename), {
    var r=Module.remote;
    if (!r || !r.frameBlob) return;
    var name=UTF8ToString(filename).split(/[\\/]/).pop() || 'gw.png';
    if (!name.toLowerCase().endsWith('.png')) name += '.png';
    var url=URL.createObjectURL(r.frameBlob), a=document.createElement('a');
    a.href=url; a.download=name; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000);
});
static void sendCommand(std::string command) {
    Utils::rtrim(command);
    if (command == "open") { EM_ASM({if(Module.viewerOpenFiles)Module.viewerOpenFiles();}); return; }
    if (command == "line") { ui->drawLine=!ui->drawLine; return; }
    if (command == "snapshot" || command.rfind("snapshot ",0) == 0 || command == "s" || command.rfind("s ",0) == 0) {
        auto pos=command.find(' '); downloadFrame(pos == std::string::npos ? "gw.png" : command.substr(pos+1).c_str()); return;
    }
    if (command == "settings" || command == "general" || command == "thresholds") {
        std::ostringstream out; Commands::run_command_map(ui.get(),command,out); return;
    }
    sendAction(1, command.c_str(),0,0,ui->regionSelection); // Resolve regions/features with the server's normal GW parser.
}
static void tick() {
    const int width = EM_ASM_INT({ return Module.canvas.parentElement.clientWidth; });
    const int height = EM_ASM_INT({ return Module.canvas.parentElement.clientHeight; });
    static int lastW=0,lastH=0,lastFW=0,lastFH=0;
    static double lastDpr=0;
    const double dpr=EM_ASM_DOUBLE({return window.devicePixelRatio || 1;});
    if (width != lastW || height != lastH || dpr != lastDpr) {
        glfwSetWindowSize(window,width,height);
        EM_ASM({ var c=document.getElementById('canvas'); c.style.width='100%'; c.style.height='100%'; });
        lastW=width; lastH=height; lastDpr=dpr;
        ui->endScaleBarSelection(); remoteDrag=false;
    }
    int fw,fh; glfwGetFramebufferSize(window,&fw,&fh);
    if (fw != lastFW || fh != lastFH) {
        lastFW=fw; lastFH=fh;
        requestSize(fw,fh,static_cast<double>(fw)/std::max(1,width));
    }
    glfwPollEvents();
    // Compose each complete frame into a private framebuffer. Never blend a new
    // UI frame into a browser swap buffer whose contents are unspecified.
    glBindTexture(GL_TEXTURE_2D,composeTexture);
    if (fw != composeW || fh != composeH) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,fw,fh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        composeW=fw; composeH=fh;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,composeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,composeTexture,0);
    glViewport(0,0,fw,fh); glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glClearColor(0.035f,0.035f,0.05f,1); glClear(GL_COLOR_BUFFER_BIT);
    // GW owns the original ImGui NewFrame/Render path. Add the remote frame to
    // its background list immediately after NewFrame via a dedicated callback.
    ui->monitorScale=static_cast<float>(fw)/std::max(1,width);
    ui->opts.dimensions={width,height};
    ui->setImageSize(fw,fh); ui->setScaling();
    ui->trackY=remoteTrackY; ui->gap=remoteGap; ui->refSpace=remoteRefSpace; ui->sliderSpace=remoteSliderSpace;
    bool pendingClose=false;
    auto background = []() {
        if (hasImage) ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(texture), ImVec2(0,0), ImGui::GetIO().DisplaySize);
        drawLocalCommandBox();
    };
    ui->overlayImGui(pendingClose, background, pointerInput);
    if (pendingClose) ui->mode=Manager::Show::SINGLE;
    if (!busy && !applyingState && !ImGui::IsAnyItemActive()) {
        if (ui->opts.theme_str != acceptedTheme) {
            acceptedTheme=ui->opts.theme_str;
            sendCommand("theme " + acceptedTheme);
        } else {
            auto settings=GwViewer::renderSettings(ui->opts);
            if (settings != acceptedSettings) {
                acceptedSettings=settings;
                sendAction(3,settings.c_str(),0,0,-1);
            }
        }
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER,composeFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0,0,fw,fh,0,0,fw,fh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glfwSwapBuffers(window);
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);
    const int w=EM_ASM_INT({return Module.canvas.clientWidth;});
    const int h=EM_ASM_INT({return Module.canvas.clientHeight;});
    window=glfwCreateWindow(w,h,"GW remote",nullptr,nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    Themes::IniOptions opts; opts.readIni(); opts.threads=1;
    opts.font_str="Default"; opts.font_size=12;
    std::vector<std::string> paths,tracks; std::vector<Utils::Region> regions;
    ui=std::make_unique<Manager::GwPlot>("",paths,opts,regions,tracks);
    ui->window=window; ui->terminalOutput=false; ui->showUIOverlay=true;
    ui->remoteCommand=sendCommand;
    acceptedSettings=GwViewer::renderSettings(ui->opts); acceptedTheme=ui->opts.theme_str;
    ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr;
    ImGui::GetCurrentContext()->ConfigNavEnableTabbing=false;
    ImFontConfig cfg; cfg.FontDataOwnedByAtlas=false; cfg.OversampleH=3; cfg.OversampleV=3;
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)GwFonts::Aileron_Regular_otf,
        (int)sizeof(GwFonts::Aileron_Regular_otf),14.f,&cfg);
    ui->monoFont=ImGui::GetIO().Fonts->AddFontDefaultVector();
    Themes::applyImGuiTheme(ui->opts.theme_str);
    glfwSetKeyCallback(window,[](GLFWwindow*,int key,int scan,int action,int mods) {
        if (ui->commandBoxOpen() || !ImGui::GetIO().WantCaptureKeyboard) {
            if (ui->commandKey(key,scan,action,mods)) return;
            if (action!=GLFW_PRESS && action!=GLFW_REPEAT) return;
            const bool navigation=key==GLFW_KEY_LEFT || key==GLFW_KEY_RIGHT || key==GLFW_KEY_UP || key==GLFW_KEY_DOWN || key==GLFW_KEY_PAGE_UP || key==GLFW_KEY_PAGE_DOWN || key==GLFW_KEY_LEFT_BRACKET || key==GLFW_KEY_RIGHT_BRACKET || key==GLFW_KEY_L || key==GLFW_KEY_F || key==GLFW_KEY_R || key==GLFW_KEY_MINUS || key==GLFW_KEY_EQUAL;
            if (navigation && (mods==0 || mods==GLFW_MOD_CONTROL || (mods==GLFW_MOD_SHIFT && key==GLFW_KEY_EQUAL)))
                sendCommand("key "+std::to_string(key)+" "+std::to_string(mods));
        }
    });
    glfwSetCharCallback(window,[](GLFWwindow*,unsigned int c) {
        if (ui->commandBoxOpen()) ui->commandChar(c);
    });
    ImGui_ImplGlfw_InitForOpenGL(window,true); ImGui_ImplOpenGL3_Init("#version 300 es");
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenTextures(1,&commandTexture); glBindTexture(GL_TEXTURE_2D,commandTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenTextures(1,&composeTexture); glBindTexture(GL_TEXTURE_2D,composeTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&composeFbo);
    remoteConnect(texture); emscripten_set_main_loop(tick,0,1);
    return 0;
}
