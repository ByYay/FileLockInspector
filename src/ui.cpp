#include "ui.h"
#include "scan_job.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <sstream>

namespace fli {
namespace {
enum : int { Refresh = 100, Advanced, CancelScan, End, Force, Details, CopyPid, CopyPath, Location, Close, List, More };
constexpr UINT_PTR Tick = 1;
class Window {
    HINSTANCE instance_;
    HWND window_ = nullptr, title_ = nullptr, pathControl_ = nullptr, list_ = nullptr, status_ = nullptr, icon_ = nullptr;
    HWND filename_ = nullptr, heading_ = nullptr, hint_ = nullptr;
    HICON fileIcon_ = nullptr;
    HIMAGELIST processIcons_ = nullptr;
    std::vector<std::pair<int, HWND>> buttons_;
    std::wstring path_, notice_;
    std::vector<Record> rows_;
    ScanJob scan_;
    Handle closing_;
    ULONGLONG closeStarted_ = 0;
    bool forcing_ = false, advanced_ = false, dark_ = false;
    HFONT font_ = nullptr, titleFont_ = nullptr, labelFont_ = nullptr;
    HBRUSH background_ = nullptr;
    COLORREF bg_ = 0, fg_ = 0, surface_ = 0, muted_ = 0, border_ = 0, accent_ = 0;
    bool contrast_ = false;
    UINT dpi_ = 96;
    int scale(int n) const { return MulDiv(n, static_cast<int>(dpi_), 96); }
    HWND button(int id) const { for(auto [key, h] : buttons_) if(key == id) return h; return nullptr; }
    int selected() const { return ListView_GetNextItem(list_, -1, LVNI_SELECTED); }
    Record* selection() { int i = selected(); return i >= 0 && size_t(i) < rows_.size() ? &rows_[i] : nullptr; }
    void text(const std::wstring& s) {
        SetWindowTextW(status_, s.c_str());
        SetWindowTextW(heading_, (L"Processes  ·  " + std::to_wstring(rows_.size())).c_str());
    }
    void enableActions() {
        auto r = selection(); bool allowed = r && r->safe && r->pid != GetCurrentProcessId() && r->state == State::Running && !closing_ && !scan_.active();
        EnableWindow(button(End), allowed);
        for(int id : {Details, More}) EnableWindow(button(id), r != nullptr);
        EnableWindow(button(Refresh), !scan_.active() && !closing_);
        EnableWindow(button(Advanced), !scan_.active() && !closing_);
        EnableWindow(button(CancelScan), scan_.active());
        ShowWindow(button(CancelScan), scan_.active() ? SW_SHOW : SW_HIDE);
        SetWindowTextW(hint_, r ? (allowed ? L"Choose an action for the selected process." : L"This process cannot be ended safely.") : L"Select a process to view details or request closure.");
    }
    std::wstring stateText(const Record& r) const {
        if(r.state == State::Exited) return L"Exited";
        if(r.state == State::AccessDenied) return L"Access denied";
        if(r.state != State::Running) return L"Unknown";
        return r.confirmed ? L"Confirmed identity (snapshot)" : L"Possible (RM report)";
    }
    void renderRow(int index) {
        auto& r = rows_[index];
        std::wstring pid = std::to_wstring(r.pid), state = stateText(r);
        const wchar_t* method = r.method == Method::Advanced ? L"Advanced" : L"Restart Manager";
        const wchar_t* action = r.safe && r.pid != GetCurrentProcessId() ? L"End Process..." : L"Unavailable";
        const wchar_t* values[] = {r.name, pid.c_str(), r.path, state.c_str(), method, action};
        for(int col = 0; col < 6; ++col) ListView_SetItemText(list_, index, col, const_cast<wchar_t*>(values[col]));
    }
    void receive(const Record& r) {
        if(r.kind != 1) return;
        for(size_t i = 0; i < rows_.size(); ++i) {
            if(rows_[i].pid == r.pid && rows_[i].created == r.created) {
                if(r.confirmed || !rows_[i].confirmed) rows_[i] = r;
                renderRow(static_cast<int>(i)); return;
            }
        }
        if(rows_.size() >= 65536) { scan_.cancel(); return; }
        rows_.push_back(r); int index = static_cast<int>(rows_.size() - 1);
        LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_IMAGE; item.iItem = index; item.iImage = 0; item.pszText = const_cast<wchar_t*>(r.name);
        ListView_InsertItem(list_, &item); renderRow(index);
    }
    void start(bool advanced) {
        advanced_ = advanced;
        // Never merge stale scan results: each refresh replaces the previous snapshot.
        rows_.clear(); ListView_DeleteAllItems(list_);
        if(!scan_.start(path_, advanced)) text(L"Failed to start scanner: " + errorText(scan_.error));
        else text(advanced ? L"Advanced scanning... Cancel is available. Folder contents are not scanned." : L"Scanning...");
        enableActions();
    }
    void finished() {
        std::wstring s = notice_; notice_.clear();
        if(scan_.error == ERROR_DIRECTORY) s += L"Folders require Advanced Scan (folder handle only; contents excluded). ";
        else if(scan_.error == ERROR_FILE_NOT_FOUND || scan_.error == ERROR_PATH_NOT_FOUND) s += L"File no longer exists. ";
        else if(scan_.error) s += L"Scan incomplete: " + errorText(scan_.error) + L" ";
        else if(scan_.incomplete) s += L"Advanced scan incomplete: some handles could not be inspected. ";
        s += rows_.empty() ? L"No processes detected. File may still be in use." : L"Processes detected: " + std::to_wstring(rows_.size()) + L". Results are a snapshot.";
        text(s); enableActions();
    }
    bool confirm(const Record& r, bool force) {
        std::wstring body = L"Process: " + std::wstring(r.name) + L"\nPID: " + std::to_wstring(r.pid) + L"\n" + r.path +
            L"\n\nEnding this process may cause unsaved data to be lost.";
        TASKDIALOG_BUTTON actions[] = {{IDOK, force ? L"Force Terminate" : L"End Process"}};
        TASKDIALOGCONFIG config{}; config.cbSize = sizeof(config); config.hwndParent = window_;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON; config.pszWindowTitle = L"File Lock Inspector";
        config.pszMainInstruction = force ? L"Force Terminate?" : L"End Process?";
        config.pszContent = body.c_str(); config.pszMainIcon = TD_WARNING_ICON;
        config.cButtons = 1; config.pButtons = actions; config.nDefaultButton = IDCANCEL;
        int result = IDCANCEL; return SUCCEEDED(TaskDialogIndirect(&config, &result, nullptr, nullptr)) && result == IDOK;
    }
    void endProcess(bool force) {
        auto current = selection(); if(!current || scan_.active() || closing_ || !current->safe) return;
        Record r = *current; std::wstring error;
        auto process = verifiedProcess(r, force, error);
        if(!process) { text(L"Cannot end process: " + error); return; }
        if(!confirm(r, force)) return;
        // Recheck after the modal confirmation; never rely on a stale enabled button.
        auto rechecked = verifiedProcess(r, force, error);
        if(!rechecked) { text(L"Cannot end process: " + error); return; }
        DWORD code = 0;
        bool ok = force ? forceTerminate(process.get(), code) : requestClose(process.get(), r.pid, code);
        if(!ok) {
            text(force ? L"Failed to terminate process: " + errorText(code) :
                L"Controlled close unavailable: " + errorText(code) + L" Select Force Terminate only if you want to stop it forcibly.");
            return;
        }
        closing_ = std::move(process); closeStarted_ = GetTickCount64(); forcing_ = force;
        text(force ? L"Termination requested; verifying process exit..." : L"Close requested; waiting for the application (up to 5 seconds)...");
        enableActions();
    }
    void details() {
        auto r = selection(); if(!r) return;
        std::wstring s = L"Process: " + std::wstring(r->name) + L"\nPID: " + std::to_wstring(r->pid) +
            L"\nCreated (FILETIME): " + std::to_wstring(r->created) + L"\nExecutable: " + r->path + L"\nUser SID: " + r->user +
            L"\nType: " + r->type + L"\nStatus: " + stateText(*r) + L"\nTermination: " +
            (r->safe ? L"Available with confirmation" : r->reason) + L"\n\nSelected resource: " + path_;
        const auto& m = scan_.metrics;
        s += L"\n\nLast scan: " + std::to_wstring(m.elapsedMs) + L" ms; CPU " + std::to_wstring(m.cpu100ns / 10000) +
            L" ms; peak working set " + std::to_wstring(m.peakWorkingSet / 1024) + L" KiB.";
        MessageBoxW(window_, s.c_str(), L"Process details", MB_OK | MB_ICONINFORMATION);
    }
    void clipboard(const std::wstring& s) {
        if(!OpenClipboard(window_)) { text(L"Clipboard unavailable."); return; }
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
        if(data) {
            void* p = GlobalLock(data);
            if(p) {
                memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(data);
                if(EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) data = nullptr;
            }
            if(data) GlobalFree(data);
        }
        CloseClipboard();
    }
    void location() {
        auto r = selection(); if(!r || !r->path[0]) return;
        PIDLIST_ABSOLUTE pidl = nullptr;
        HRESULT hr = SHParseDisplayName(r->path, nullptr, &pidl, 0, nullptr);
        if(SUCCEEDED(hr)) { hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); CoTaskMemFree(pidl); }
        if(FAILED(hr)) text(L"Could not open executable location: " + errorText(static_cast<DWORD>(hr)));
    }
    void more() {
        auto r = selection(); if(!r) return;
        HMENU menu = CreatePopupMenu(); if(!menu) return;
        AppendMenuW(menu, MF_STRING, CopyPid, L"Copy PID");
        AppendMenuW(menu, MF_STRING | (r->path[0] ? 0 : MF_GRAYED), CopyPath, L"Copy executable path");
        AppendMenuW(menu, MF_STRING | (r->path[0] ? 0 : MF_GRAYED), Location, L"Open file location");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        bool allowed = r->safe && r->pid != GetCurrentProcessId() && r->state == State::Running && !closing_ && !scan_.active();
        AppendMenuW(menu, MF_STRING | (allowed ? 0 : MF_GRAYED), Force, L"Force Terminate...");
        RECT rect{}; GetWindowRect(button(More), &rect);
        int action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN, rect.right, rect.bottom, 0, window_, nullptr);
        DestroyMenu(menu);
        if(action) SendMessageW(window_, WM_COMMAND, action, 0);
    }
    LRESULT drawButton(NMCUSTOMDRAW& draw) {
        if(contrast_ || draw.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
        bool disabled = !IsWindowEnabled(draw.hdr.hwndFrom);
        bool hot = (draw.uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
        bool primary = draw.hdr.idFrom == Refresh;
        COLORREF fill = primary && !disabled ? accent_ : hot ? (dark_ ? RGB(51,59,72) : RGB(230,237,247)) : surface_;
        HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : border_);
        auto oldBrush = SelectObject(draw.hdc, brush); auto oldPen = SelectObject(draw.hdc, pen);
        RECT rect = draw.rc; InflateRect(&rect,-1,-1);
        RoundRect(draw.hdc,rect.left,rect.top,rect.right,rect.bottom,scale(8),scale(8));
        SelectObject(draw.hdc,oldBrush); SelectObject(draw.hdc,oldPen); DeleteObject(brush); DeleteObject(pen);
        wchar_t label[128]{}; GetWindowTextW(draw.hdr.hwndFrom,label,128);
        SetBkMode(draw.hdc,TRANSPARENT); SetTextColor(draw.hdc,disabled ? muted_ : primary ? RGB(255,255,255) : fg_);
        auto oldFont = SelectObject(draw.hdc,font_);
        DrawTextW(draw.hdc,label,-1,&rect,DT_CENTER | DT_VCENTER | DT_SINGLELINE |
            ((SendMessageW(draw.hdr.hwndFrom,WM_QUERYUISTATE,0,0) & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));
        SelectObject(draw.hdc,oldFont);
        if((draw.uItemState & CDIS_FOCUS) && !(SendMessageW(draw.hdr.hwndFrom,WM_QUERYUISTATE,0,0) & UISF_HIDEFOCUS)) {
            InflateRect(&rect,-scale(4),-scale(4)); DrawFocusRect(draw.hdc,&rect);
        }
        return CDRF_SKIPDEFAULT;
    }
    static LRESULT CALLBACK listProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Window*>(data);
        if(msg == WM_NOTIFY && reinterpret_cast<NMHDR*>(l)->code == NM_CUSTOMDRAW && !self->contrast_) {
            auto draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
            if(draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if(draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                HBRUSH brush = CreateSolidBrush(self->surface_); FillRect(draw->hdc,&draw->rc,brush); DeleteObject(brush);
                wchar_t label[128]{}; HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = label; item.cchTextMax = 128;
                Header_GetItem(draw->hdr.hwndFrom,static_cast<int>(draw->dwItemSpec),&item);
                RECT rect = draw->rc; rect.left += self->scale(10);
                SetBkMode(draw->hdc,TRANSPARENT); SetTextColor(draw->hdc,self->muted_);
                auto old = SelectObject(draw->hdc,self->labelFont_);
                DrawTextW(draw->hdc,label,-1,&rect,DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                SelectObject(draw->hdc,old); return CDRF_SKIPDEFAULT;
            }
        }
        return DefSubclassProc(h,msg,w,l);
    }
    void theme() {
        DWORD light = 1, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        HIGHCONTRASTW hc{sizeof(hc)}; SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
        contrast_ = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        dark_ = !light && !contrast_;
        bg_ = contrast_ ? GetSysColor(COLOR_WINDOW) : dark_ ? RGB(23,26,32) : RGB(246,248,251);
        fg_ = contrast_ ? GetSysColor(COLOR_WINDOWTEXT) : dark_ ? RGB(235,239,246) : RGB(28,37,52);
        surface_ = contrast_ ? bg_ : dark_ ? RGB(31,36,44) : RGB(255,255,255);
        muted_ = contrast_ ? fg_ : dark_ ? RGB(166,177,194) : RGB(91,106,126);
        border_ = contrast_ ? fg_ : dark_ ? RGB(61,70,83) : RGB(218,225,235);
        accent_ = RGB(35,103,206);
        if(background_) DeleteObject(background_); background_ = CreateSolidBrush(bg_);
        BOOL dark = dark_; DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
        ListView_SetBkColor(list_, surface_); ListView_SetTextBkColor(list_, surface_); ListView_SetTextColor(list_, fg_);
        // Keep documented native button themes for accessibility; no undocumented theme ordinals.
        SetWindowTheme(list_, L"Explorer", nullptr);
        SetWindowTheme(ListView_GetHeader(list_), contrast_ ? nullptr : L"", contrast_ ? nullptr : L"");
        InvalidateRect(window_, nullptr, TRUE); InvalidateRect(list_, nullptr, TRUE);
    }
    void font() {
        HFONT next = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if(!next) return;
        for(HWND h : {pathControl_, list_, status_, hint_}) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        for(auto [id,h] : buttons_) { (void)id; SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE); }
        if(font_) DeleteObject(font_); font_ = next;
        HFONT title = CreateFontW(-scale(24),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HFONT label = CreateFontW(-scale(14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(title) { SendMessageW(title_,WM_SETFONT,reinterpret_cast<WPARAM>(title),TRUE); if(titleFont_) DeleteObject(titleFont_); titleFont_ = title; }
        if(label) {
            for(HWND h : {filename_,heading_,ListView_GetHeader(list_)}) SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(label),TRUE);
            if(labelFont_) DeleteObject(labelFont_); labelFont_ = label;
        }
    }
    void layout() {
        RECT rect{}; GetClientRect(window_, &rect); int width = rect.right, height = rect.bottom;
        int pad = scale(24), bh = scale(36), bottom = height - scale(170);
        MoveWindow(title_,pad,scale(24),width-pad*2,scale(34),TRUE);
        MoveWindow(icon_,pad,scale(77),scale(24),scale(24),TRUE);
        MoveWindow(filename_,pad+scale(32),scale(74),width-pad*2-scale(32),scale(24),TRUE);
        MoveWindow(pathControl_,pad+scale(32),scale(102),width-pad*2-scale(32),scale(24),TRUE);
        MoveWindow(heading_,pad,scale(149),width-pad*2,scale(24),TRUE);
        MoveWindow(list_,pad,scale(181),width-pad*2,(std::max)(scale(100),bottom-scale(193)),TRUE);
        MoveWindow(hint_,pad,bottom+scale(8),(std::max)(0,width-pad*2-scale(360)),scale(24),TRUE);
        int x = width-pad-scale(340);
        for(int id : {Details,End,More}) {
            int bw = scale(id == End ? 136 : 94);
            MoveWindow(button(id),x,bottom,bw,bh,TRUE); x += bw+scale(8);
        }
        MoveWindow(status_,pad,bottom+scale(52),width-pad*2,scale(50),TRUE);
        x = pad;
        for(int id : {Refresh, Advanced, CancelScan}) {
            int bw = scale(id == Advanced ? 142 : 110);
            MoveWindow(button(id), x, height - pad - bh, bw, bh, TRUE); x += bw + scale(8);
        }
        MoveWindow(button(Close), width - pad - scale(100), height - pad - bh, scale(100), bh, TRUE);
        int fixed = scale(160+66+170+136+118);
        const int widths[] = {scale(160),scale(66),(std::max)(scale(180),width-pad*2-fixed-scale(22)),scale(170),scale(136),scale(118)};
        for(int i=0;i<6;++i) ListView_SetColumnWidth(list_,i,widths[i]);
    }
    void create() {
        dpi_ = GetDpiForWindow(window_);
        auto make = [&](const wchar_t* cls, const wchar_t* label, DWORD style, int id) {
            return CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, 0,0,0,0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        };
        icon_ = make(L"STATIC",L"",SS_ICON,0);
        SHSTOCKICONINFO stock{sizeof(stock)};
        if(SUCCEEDED(SHGetStockIconInfo(SIID_DOCNOASSOC,SHGSI_ICON | SHGSI_SMALLICON,&stock))) {
            fileIcon_ = stock.hIcon; SendMessageW(icon_,STM_SETICON,reinterpret_cast<WPARAM>(fileIcon_),0);
        }
        title_ = make(L"STATIC", L"File Lock Inspector", SS_LEFT, 0);
        filename_ = make(L"STATIC",baseName(path_).c_str(),SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX,0);
        heading_ = make(L"STATIC",L"Processes",SS_LEFT,0);
        hint_ = make(L"STATIC",L"Select a process to view details.",SS_LEFT | SS_ENDELLIPSIS,0);
        pathControl_ = make(L"EDIT", path_.c_str(), ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, 0);
        list_ = make(WC_LISTVIEWW, L"Processes using this file", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | WS_TABSTOP, List);
        SetWindowSubclass(list_,listProc,1,reinterpret_cast<DWORD_PTR>(this));
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        processIcons_ = ImageList_Create(scale(20),scale(32),ILC_COLOR32 | ILC_MASK,1,0);
        stock = {}; stock.cbSize = sizeof(stock);
        if(processIcons_ && SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION,SHGSI_ICON | SHGSI_SMALLICON,&stock))) {
            ImageList_AddIcon(processIcons_,stock.hIcon); DestroyIcon(stock.hIcon);
            ListView_SetImageList(list_,processIcons_,LVSIL_SMALL);
        }
        const wchar_t* names[] = {L"Process", L"PID", L"Executable path", L"Access state", L"Detection", L"Action"};
        const int widths[] = {150, 70, 300, 205, 140, 130, 125};
        for(int i = 0; i < 6; ++i) {
            LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = const_cast<wchar_t*>(names[i]); col.cx = scale(widths[i]);
            ListView_InsertColumn(list_, i, &col);
        }
        status_ = make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
        const std::pair<int,const wchar_t*> labels[] = {{Refresh,L"&Refresh"},{Advanced,L"&Advanced Scan"},{CancelScan,L"Cancel scan"},
            {End,L"&End Process"},{Details,L"&Details"},{More,L"&More..."},{Close,L"&Close"}};
        for(auto [id,label] : labels) buttons_.emplace_back(id, make(L"BUTTON", label, BS_PUSHBUTTON | WS_TABSTOP, id));
        font(); theme(); layout(); SetTimer(window_, Tick, 50, nullptr); start(false);
    }
    LRESULT message(UINT msg, WPARAM w, LPARAM l) {
        switch(msg) {
        case WM_CREATE: create(); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto m = reinterpret_cast<MINMAXINFO*>(l); m->ptMinTrackSize = {scale(880), scale(560)}; return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(w); auto r = reinterpret_cast<RECT*>(l);
            SetWindowPos(window_, nullptr, r->left,r->top,r->right-r->left,r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            font(); layout(); return 0;
        }
        case WM_SETTINGCHANGE: case WM_THEMECHANGED: theme(); return 0;
        case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(w);
            HWND control = reinterpret_cast<HWND>(l);
            SetTextColor(dc,control == status_ || control == hint_ || control == pathControl_ ? muted_ : fg_);
            SetBkColor(dc, bg_); return reinterpret_cast<LRESULT>(background_);
        }
        case WM_ERASEBKGND: { RECT r{}; GetClientRect(window_, &r); FillRect(reinterpret_cast<HDC>(w), &r, background_); return 1; }
        case WM_NOTIFY: {
            auto hdr = reinterpret_cast<NMHDR*>(l);
            if(hdr->code == NM_CUSTOMDRAW && hdr->idFrom != List) return drawButton(*reinterpret_cast<NMCUSTOMDRAW*>(l));
            if(hdr->idFrom == List) {
                if(hdr->code == LVN_ITEMCHANGED) enableActions();
                if(hdr->code == NM_DBLCLK) details();
                if(hdr->code == NM_CLICK) {
                    auto click = reinterpret_cast<NMITEMACTIVATE*>(l);
                    if(click->iItem >= 0 && click->iSubItem == 5) endProcess(false);
                }
            }
            return 0;
        }
        case WM_COMMAND:
            switch(LOWORD(w)) {
            case Refresh: start(false); break;
            case Advanced: start(true); break;
            case CancelScan: scan_.error = ERROR_CANCELLED; scan_.cancel(); finished(); break;
            case End: endProcess(false); break;
            case Force: endProcess(true); break;
            case Details: details(); break;
            case More: more(); break;
            case CopyPid: if(auto r = selection()) clipboard(std::to_wstring(r->pid)); break;
            case CopyPath: if(auto r = selection()) clipboard(r->path); break;
            case Location: location(); break;
            case Close: case IDCANCEL: DestroyWindow(window_); break;
            }
            return 0;
        case WM_TIMER:
            if(scan_.active()) { scan_.poll([this](const Record& r) { receive(r); }); if(!scan_.active()) finished(); }
            if(closing_) {
                if(WaitForSingleObject(closing_.get(), 0) == WAIT_OBJECT_0) {
                    closing_.reset(); notice_ = L"Process terminated successfully. "; start(advanced_);
                } else if(GetTickCount64() - closeStarted_ >= 5000) {
                    closing_.reset(); text(forcing_ ? L"Termination not yet confirmed. Refresh to check; file may still be in use." :
                        L"Process is still running. You may choose Force Terminate (separate confirmation required)."); enableActions();
                }
            }
            return 0;
        case WM_CLOSE: DestroyWindow(window_); return 0;
        case WM_DESTROY: KillTimer(window_, Tick); scan_.cancel(); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window_, msg, w, l);
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if(m == WM_NCCREATE) {
            self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if(!self) return DefWindowProcW(h,m,w,l);
        try { return self->message(m,w,l); }
        catch(...) { self->scan_.cancel(); MessageBoxW(h,L"The operation failed. No process was automatically terminated.",L"File Lock Inspector",MB_OK | MB_ICONERROR); return 0; }
    }
public:
    Window(HINSTANCE instance, std::wstring path) : instance_(instance), path_(std::move(path)) {}
    ~Window() {
        if(font_) DeleteObject(font_); if(background_) DeleteObject(background_);
        if(titleFont_) DeleteObject(titleFont_); if(labelFont_) DeleteObject(labelFont_);
        if(fileIcon_) DestroyIcon(fileIcon_); if(processIcons_) ImageList_Destroy(processIcons_);
    }
    int run(int show) {
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = proc; cls.hInstance = instance_; cls.lpszClassName = L"FileLockInspector.Window";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        if(!RegisterClassExW(&cls)) return 1;
        HWND h = CreateWindowExW(0,cls.lpszClassName,L"File Lock Inspector",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1200,650,nullptr,nullptr,instance_,this);
        if(!h) return 1;
        ShowWindow(h,show); UpdateWindow(h);
        MSG message{}; BOOL result;
        while((result = GetMessageW(&message,nullptr,0,0)) > 0) {
            if(message.message == WM_KEYDOWN && message.wParam == VK_F5) { if(!scan_.active() && !closing_) start(false); continue; }
            if(!IsDialogMessageW(h,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return result == -1 ? 1 : 0;
    }
};
}
int runUi(HINSTANCE instance, const std::wstring& path, int show) { return Window(instance,path).run(show); }
}
