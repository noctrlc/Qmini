#include "panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WID  350
#define HGT   40
#define EH_M  170
#define EH_J  210

#define XBG      0x1A1A2E
#define XSURF    0x16213E
#define XBORDER  0x2A2A4E
#define XTXT     0xE0E0E0
#define XDIM     0x888888
#define XACC     0x46A0E6
#define XGRN     0x2ECC71
#define XRED     0xE74C3C
#define XORG     0xF39C12
#define XBLU     0x4A90D9
#define XHOV     0x252540
#define XPRS     0x151525
#define XGBG     0x0A2A0A

static struct {
    panel_t *p; HWND hwnd; HFONT f1,f2,fb; int ex,pin,hv,pr,im;
    char js[64],jr[32],jn[32],jp[64];
    HWND es,er,en,ep, ok,no;
    wchar_t mb[12][64]; int nm;
} g;

typedef struct { int id; RECT r; } btn_t;
static btn_t btns[16]; static int nb;

static COLORREF C(UINT32 c){return RGB((c>>16)&0xFF,(c>>8)&0xFF,c&0xFF);}
static HBRUSH B(UINT32 c){
    static HBRUSH ch[16]; static UINT32 ck[16]; static int cn=0;
    for(int i=0;i<cn;i++) if(ck[i]==c)return ch[i];
    if(cn<16){ck[cn]=c;ch[cn]=CreateSolidBrush(C(c));return ch[cn++];}
    return ch[0];
}

static void ab(int id,int x,int y,int w,int h){
    if(nb<16){btns[nb].id=id;SetRect(&btns[nb].r,x,y,x+w,y+h);nb++;}
}
static int ht(int mx,int my){
    for(int i=nb-1;i>=0;i--) if(PtInRect(&btns[i].r,*(POINT*)&(POINT){mx,my}))return btns[i].id;
    return 0;
}

static void pb(HDC dc,btn_t*b,const wchar_t*t,UINT32 bg,UINT32 fg,HFONT f){
    UINT32 c=(g.pr==b->id)?XPRS:(g.hv==b->id)?XHOV:bg;
    RECT r=b->r; HBRUSH br=B(c);
    HPEN pn=CreatePen(PS_SOLID,1,C(XBORDER));HPEN op=SelectObject(dc,pn);
    HBRUSH ob=SelectObject(dc,br);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,6,6);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(pn);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,C(fg));SelectObject(dc,f);
    DrawTextW(dc,t,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void paint(HDC dc){
    RECT R;GetClientRect(g.hwnd,&R);int W=R.right,H=R.bottom;
    FillRect(dc,&R,B(XBG));
    int co=(g.p->server[0]!=0); nb=0;

    if(g.ex==2){
        SelectObject(dc,g.f1);SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,C(XTXT));RECT tr={6,4,200,20};DrawTextW(dc,L"Qmini  ○ 加入房间",-1,&tr,DT_LEFT|DT_VCENTER);
        SetTextColor(dc,C(XACC));RECT mr={W-70,4,W-4,20};DrawTextW(dc,L"Mlaiou",-1,&mr,DT_RIGHT|DT_VCENTER);
        RECT ln={6,26,W-12,27};FillRect(dc,&ln,B(XBORDER));
        SetTextColor(dc,C(XDIM));SelectObject(dc,g.f2);
        RECT l1={10,30,120,44};DrawTextW(dc,L"服务器地址",-1,&l1,DT_LEFT|DT_VCENTER);
        RECT l2={10,76,120,90};DrawTextW(dc,L"房间 / 昵称",-1,&l2,DT_LEFT|DT_VCENTER);
        RECT l3={10,122,120,136};DrawTextW(dc,L"密码 (留空不加密)",-1,&l3,DT_LEFT|DT_VCENTER);
        SetWindowPos(g.es,NULL,10,45,W-20,22,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g.er,NULL,10,91,(W-28)/2,22,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g.en,NULL,18+(W-28)/2,91,(W-28)/2,22,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g.ep,NULL,10,137,W-20,22,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g.ok,NULL,10,168,(W-28)/2,26,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g.no,NULL,18+(W-28)/2,168,(W-28)/2,26,SWP_NOZORDER|SWP_NOACTIVATE);
        ShowWindow(g.es,SW_SHOW);ShowWindow(g.er,SW_SHOW);ShowWindow(g.en,SW_SHOW);
        ShowWindow(g.ep,SW_SHOW);ShowWindow(g.ok,SW_SHOW);ShowWindow(g.no,SW_SHOW);
        ab(20,10,168,(W-28)/2,26);ab(21,18+(W-28)/2,168,(W-28)/2,26);
        pb(dc,&btns[nb-2],L"确定",XBLU,XTXT,g.f1);
        pb(dc,&btns[nb-1],L"取消",XSURF,XDIM,g.f1);
    }else if(g.ex==1){
        SelectObject(dc,g.f1);SetBkMode(dc,TRANSPARENT);
        wchar_t hd[128];_snwprintf(hd,128,L"● %hs @ %hs",g.p->room,g.p->server);
        SetTextColor(dc,C(XTXT));RECT tr={6,4,W-70,20};DrawTextW(dc,hd,-1,&tr,DT_LEFT|DT_VCENTER);
        SetTextColor(dc,C(XACC));RECT mr={W-70,4,W-4,20};DrawTextW(dc,L"Mlaiou",-1,&mr,DT_RIGHT|DT_VCENTER);
        RECT ml={6,24,W-12,H-42};FillRect(dc,&ml,B(XSURF));
        SelectObject(dc,g.f2);SetBkMode(dc,TRANSPARENT);
        for(int i=0;i<g.nm&&i<7;i++){
            int y=28+i*20; int me=(i==0);
            if(me){RECT sr={8,y-1,W-16,20};FillRect(dc,&sr,B(XGBG));}
            RECT dr={14,y+3,22,y+15};SetTextColor(dc,me?C(XGRN):C(XDIM));
            DrawTextW(dc,me?L"●":L"○",-1,&dr,DT_CENTER|DT_VCENTER);
            SetTextColor(dc,me?C(XGRN):C(XTXT));
            RECT nr={24,y,200,y+18};DrawTextW(dc,g.mb[i],-1,&nr,DT_LEFT|DT_VCENTER);
        }
        ab(1,4,H-18,38,15);ab(2,44,H-18,72,15);ab(3,118,H-18,38,15);
        ab(4,200,H-18,26,15);ab(5,228,H-18,24,15);ab(6,270,H-18,74,15);
        pb(dc,&btns[nb-6],g.p->muted?L"🔇 静音":L"🔇",g.p->muted?XRED:XSURF,g.p->muted?XTXT:XDIM,g.f2);
        pb(dc,&btns[nb-5],L"🎤 说话",XGRN,XTXT,g.f2);
        pb(dc,&btns[nb-4],L"📢",XSURF,XDIM,g.f2);
        pb(dc,&btns[nb-3],L"📌",g.pin?XACC:XSURF,g.pin?XTXT:XDIM,g.f2);
        pb(dc,&btns[nb-2],L"👥▴",XSURF,XDIM,g.f2);
        pb(dc,&btns[nb-1],L"✕ 离开",XRED,XTXT,g.f2);
    }else{
        SelectObject(dc,g.f1);SetBkMode(dc,TRANSPARENT);
        if(co){
            wchar_t s[64];_snwprintf(s,64,L"● %hs · 👥 %d",g.p->room,g.nm);
            SetTextColor(dc,C(XGRN));RECT tr={6,12,W-70,20};DrawTextW(dc,s,-1,&tr,DT_LEFT|DT_VCENTER);
            if(g.p->peak_pct>0){
                wchar_t vm[64];int blk=(g.p->peak_pct*8)/100;if(blk>8)blk=8;
                wchar_t br[16];int i;for(i=0;i<blk;i++)br[i]=L'█';for(;i<8;i++)br[i]=L'░';br[8]=0;
                _snwprintf(vm,64,L"🎤%s %d%%",br,g.p->peak_pct);
                SelectObject(dc,g.f2);SetTextColor(dc,C(XGRN));
                RECT vr={6,24,200,38};DrawTextW(dc,vm,-1,&vr,DT_LEFT|DT_VCENTER);
            }
            SetTextColor(dc,C(XACC));RECT mr={W-70,12,W-4,20};DrawTextW(dc,L"Mlaiou",-1,&mr,DT_RIGHT|DT_VCENTER);
            RECT ln={4,22,W-8,23};FillRect(dc,&ln,B(XBORDER));
            ab(1,4,24,38,15);ab(2,44,24,72,15);ab(3,118,24,38,15);
            ab(4,200,24,26,15);ab(5,228,24,24,15);ab(6,270,24,74,15);
            pb(dc,&btns[nb-6],g.p->muted?L"🔇 静音":L"🔇",g.p->muted?XRED:XSURF,g.p->muted?XTXT:XDIM,g.f2);
            pb(dc,&btns[nb-5],L"🎤 说话",XGRN,XTXT,g.f2);
            pb(dc,&btns[nb-4],L"📢",XSURF,XDIM,g.f2);
            pb(dc,&btns[nb-3],L"📌",g.pin?XACC:XSURF,g.pin?XTXT:XDIM,g.f2);
            pb(dc,&btns[nb-2],L"👥▾",XSURF,XDIM,g.f2);
            pb(dc,&btns[nb-1],L"✕ 离开",XRED,XTXT,g.f2);
        }else{
            SetTextColor(dc,C(XDIM));RECT tr={6,12,100,20};DrawTextW(dc,L"○ 未连接",-1,&tr,DT_LEFT|DT_VCENTER);
            SetTextColor(dc,C(XACC));RECT mr={W-130,12,W-80,20};DrawTextW(dc,L"Mlaiou",-1,&mr,DT_RIGHT|DT_VCENTER);
            ab(7,W-74,2,68,20);pb(dc,&btns[nb-1],L"+ 加入",XBLU,XTXT,g.f1);
        }
    }
}

static LRESULT CALLBACK wp(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:{
        CREATESTRUCT*cs=(CREATESTRUCT*)l;g.p=(panel_t*)cs->lpCreateParams;g.hwnd=h;
        g.f1=CreateFontW(15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        g.f2=CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        HINSTANCE hi=cs->hInstance;
        g.es=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"192.144.133.168:9088",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,100,20,h,(HMENU)1,hi,NULL);
        g.er=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"default",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,100,20,h,(HMENU)2,hi,NULL);
        g.en=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"Player",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,100,20,h,(HMENU)3,hi,NULL);
        g.ep=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL,0,0,100,20,h,(HMENU)4,hi,NULL);
        g.ok=CreateWindowExW(0,L"BUTTON",L"确定",WS_CHILD|BS_PUSHBUTTON,0,0,80,24,h,(HMENU)10,hi,NULL);
        g.no=CreateWindowExW(0,L"BUTTON",L"取消",WS_CHILD|BS_PUSHBUTTON,0,0,80,24,h,(HMENU)11,hi,NULL);
        ShowWindow(g.es,SW_HIDE);ShowWindow(g.er,SW_HIDE);ShowWindow(g.en,SW_HIDE);
        ShowWindow(g.ep,SW_HIDE);ShowWindow(g.ok,SW_HIDE);ShowWindow(g.no,SW_HIDE);
        g.im=INPUT_MODE_PTT;return 0;
    }
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);
        HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,WID,250);
        SelectObject(mem,bmp);paint(mem);
        RECT R;GetClientRect(h,&R);BitBlt(dc,0,0,R.right,R.bottom,mem,0,0,SRCCOPY);
        DeleteDC(mem);DeleteObject(bmp);EndPaint(h,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:g.pr=ht(LOWORD(l),HIWORD(l));InvalidateRect(h,NULL,FALSE);return 0;
    case WM_LBUTTONUP:{
        int id=ht(LOWORD(l),HIWORD(l));g.pr=0;InvalidateRect(h,NULL,FALSE);
        switch(id){
        case 7:g.ex=2;SetWindowPos(h,NULL,0,0,WID,EH_J,SWP_NOZORDER|SWP_NOMOVE);InvalidateRect(h,NULL,FALSE);return 0;
        case 6:PostMessageW(h,WM_COMMAND,PANEL_CMD_LEAVE_ROOM,0);return 0;
        case 1:PostMessageW(h,WM_COMMAND,PANEL_CMD_MODE_MUTED,0);return 0;
        case 2:PostMessageW(h,WM_COMMAND,PANEL_CMD_MODE_PTT,0);return 0;
        case 3:PostMessageW(h,WM_COMMAND,PANEL_CMD_MODE_OPEN,0);return 0;
        case 4:g.pin=!g.pin;SetWindowPos(h,g.pin?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOMOVE);InvalidateRect(h,NULL,FALSE);return 0;
        case 5:g.ex=(g.ex==1)?0:1;if(g.ex==0){ShowWindow(g.es,SW_HIDE);ShowWindow(g.er,SW_HIDE);ShowWindow(g.en,SW_HIDE);ShowWindow(g.ep,SW_HIDE);ShowWindow(g.ok,SW_HIDE);ShowWindow(g.no,SW_HIDE);}SetWindowPos(h,NULL,0,0,WID,g.ex?EH_M:HGT,SWP_NOZORDER|SWP_NOMOVE);InvalidateRect(h,NULL,FALSE);return 0;
        case 20:GetWindowTextA(g.es,g.js,64);GetWindowTextA(g.er,g.jr,32);GetWindowTextA(g.en,g.jn,32);GetWindowTextA(g.ep,g.jp,64);g.ex=0;ShowWindow(g.es,SW_HIDE);ShowWindow(g.er,SW_HIDE);ShowWindow(g.en,SW_HIDE);ShowWindow(g.ep,SW_HIDE);ShowWindow(g.ok,SW_HIDE);ShowWindow(g.no,SW_HIDE);SetWindowPos(h,NULL,0,0,WID,HGT,SWP_NOZORDER|SWP_NOMOVE);InvalidateRect(h,NULL,FALSE);PostMessageW(h,WM_COMMAND,PANEL_CMD_JOIN_ROOM,0);return 0;
        case 21:g.ex=0;ShowWindow(g.es,SW_HIDE);ShowWindow(g.er,SW_HIDE);ShowWindow(g.en,SW_HIDE);ShowWindow(g.ep,SW_HIDE);ShowWindow(g.ok,SW_HIDE);ShowWindow(g.no,SW_HIDE);SetWindowPos(h,NULL,0,0,WID,HGT,SWP_NOZORDER|SWP_NOMOVE);InvalidateRect(h,NULL,FALSE);return 0;
        }return 0;}
    case WM_MOUSEMOVE:{int pv=g.hv;g.hv=ht(LOWORD(l),HIWORD(l));if(g.hv!=pv)InvalidateRect(h,NULL,FALSE);TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,h,0};TrackMouseEvent(&tme);return 0;}
    case WM_MOUSELEAVE:g.hv=0;InvalidateRect(h,NULL,FALSE);return 0;
    case WM_COMMAND:PostMessageW(h,WM_COMMAND,w,l);return 0;
    case WM_CLOSE:PostMessageW(h,WM_COMMAND,PANEL_CMD_EXIT,0);return 0;
    case WM_DESTROY:DeleteObject(g.f1);DeleteObject(g.f2);PostQuitMessage(0);return 0;
    case WM_NCHITTEST:{LRESULT ht=DefWindowProcW(h,m,w,l);return ht==HTCLIENT?HTCAPTION:ht;}
    }
    return DefWindowProcW(h,m,w,l);
}

int panel_create(panel_t*p,HINSTANCE i){static int r=0;
    if(!r){WNDCLASSW wc={0};wc.lpfnWndProc=wp;wc.hInstance=i;wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=NULL;wc.lpszClassName=L"QHUD";RegisterClassW(&wc);r=1;}
    p->hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"QHUD",L"Q",WS_POPUP|WS_THICKFRAME,CW_USEDEFAULT,CW_USEDEFAULT,WID,HGT,NULL,NULL,i,p);
    if(!p->hwnd)return 0;p->inst=i;p->muted=0;p->peak_pct=0;p->member_count=0;p->server[0]=0;p->room[0]=0;
    ShowWindow(p->hwnd,SW_SHOW);UpdateWindow(p->hwnd);return 1;}
void panel_destroy(panel_t*p){if(p->hwnd)DestroyWindow(p->hwnd);memset(p,0,sizeof(*p));}
void panel_set_muted(panel_t*p,int v){p->muted=v;InvalidateRect(p->hwnd,NULL,FALSE);}
void panel_set_input_mode(panel_t*p,int v){g.im=v;p->muted=(v==INPUT_MODE_MUTED);InvalidateRect(p->hwnd,NULL,FALSE);}
void panel_set_connection(panel_t*p,const char*s,const char*r){
    if(s){strncpy(p->server,s,63);p->server[63]=0;}if(r){strncpy(p->room,r,31);p->room[31]=0;}
    if(!s||!s[0]){g.ex=0;SetWindowPos(p->hwnd,NULL,0,0,WID,HGT,SWP_NOZORDER|SWP_NOMOVE);}InvalidateRect(p->hwnd,NULL,FALSE);}
void panel_set_volume(panel_t*p,int v){p->peak_pct=v;if(!g.ex)InvalidateRect(p->hwnd,NULL,FALSE);}
void panel_set_members(panel_t*p,const char*n[],int c){
    p->member_count=c;g.nm=c;for(int i=0;i<c&&i<12;i++){MultiByteToWideChar(CP_UTF8,0,n[i],-1,g.mb[i],64);}InvalidateRect(p->hwnd,NULL,FALSE);}
const char*panel_get_join_server(){return g.js;}
const char*panel_get_join_room(){return g.jr;}
const char*panel_get_join_nick(){return g.jn;}
const char*panel_get_join_pass(){return g.jp;}
