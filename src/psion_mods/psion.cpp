#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#ifndef _WIN32
#include <sys/time.h>
#endif
#include <time.h>
#include "queue"
#include <vector>
#include <algorithm>


#include "shell.h"
#include "keyboard.h"
#include "timer.h"
#include "psion.h"
#include "ocr.h"
#include "memscan.h"
#include "chess.h"
#include "mem.h"
#include "regs.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <fcntl.h>
#include <io.h>
#endif

unsigned int GetTicks(){
    static bool firsttime=true;
#ifndef _WIN32
    double static msec_first;
    double msec;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    msec=tv.tv_sec*1000+(tv.tv_usec+500)/1000.0;
    if(firsttime){
	msec_first=msec;
	firsttime=false;
    }
    return (unsigned int)(msec-msec_first);
#else
    // GetTickCount() is too coarse (10-16 ms) for the timing here.
    static LARGE_INTEGER frequency,first;
    LARGE_INTEGER now;
    if(firsttime){
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&first);
	firsttime=false;
    }
    QueryPerformanceCounter(&now);
    return (unsigned int)((now.QuadPart-first.QuadPart)*1000/frequency.QuadPart);
#endif	
}

void Delay(int msec){
#ifndef _WIN32
  struct timespec tm;
  tm.tv_sec=msec/1000;
  tm.tv_nsec=1000000*(msec%1000);
  nanosleep(&tm,NULL);
#else
  Sleep(msec);
#endif
}

namespace psion {

    static bool uci_mode=false;
    static bool verbose=false;

    void SetVerbose(bool on){
	verbose=on;
    }

    bool Verbose(){
	return verbose;
    }

    // Writes complete lines only, each with a single write, so that a GUI
    // that merges stderr with stdout never splits a line of either.
    void WriteLines(FILE *out, std::string &pending, const char *text){
	pending+=text;
	size_t end=pending.rfind('\n');
	if(end==std::string::npos){
	    return;
	}
	fwrite(pending.data(),1,end+1,out);
	fflush(out);
	pending.erase(0,end+1);
    }

    // Diagnostics go to stdout in the interactive modes. In UCI mode,
    // where stdout is reserved for the protocol, they go to stderr, and
    // only with --verbose (or "debug on").
    static void say(const char *format, ...){
	static std::string pending;
	if(uci_mode && !verbose){
	    return;
	}
	char buf[4096];
	va_list args;
	va_start(args,format);
	vsnprintf(buf,sizeof(buf),format,args);
	va_end(args);
	WriteLines(uci_mode?stderr:stdout,pending,buf);
    }

    bool validate_move(std::string move){
	if(move[0]<'A'){
	    return false;
	}
	if(move[0]>'H'){
	    return false;
	}
	if(move[1]<'1'){
	    return false;
	}
	if(move[1]>'8'){
	    return false;
	}
	if(move[2]<'A'){
	    return false;
	}
	if(move[2]>'H'){
	    return false;
	}
	if(move[3]<'1'){
	    return false;
	}
	if(move[3]>'8'){
	    return false;
	}
	return true;
	
    }
    class ParsedScreen {
    public:
	std::string header[4];
	std::string status_line[3];;
	std::vector<std::string> moves;
	ParsedScreen(char txt_buf[TEXT_HEIGHT][TEXT_WIDTH]);
	void print();
	bool status;
    };

    ParsedScreen::ParsedScreen(char txt_buf[TEXT_HEIGHT][TEXT_WIDTH]){
	int i;
	std::string move;
	header[0]=std::string(txt_buf[0],TEXT_WIDTH);
	header[1]=std::string(txt_buf[1],TEXT_WIDTH);
	header[2]=std::string(txt_buf[2],TEXT_WIDTH);
	header[3]=std::string(txt_buf[3],TEXT_WIDTH);
	status_line[0]=std::string(txt_buf[20],TEXT_WIDTH);
	status_line[1]=std::string(txt_buf[22],TEXT_WIDTH);
	status_line[2]=std::string(txt_buf[24],TEXT_WIDTH);
	status=true;
	for(i=0;i<5;i++){
	    // TODO promotion piece
	    move=std::string(txt_buf[9+2*i]);
	    move=move.substr(6,2)+move.substr(9,2);
	    if(move=="    "){
		break;
	    }
	    status=validate_move(move);
	    if(!status){
		break;
	    }
	    moves.push_back(move);
	    move=std::string(txt_buf[10+2*i]);
	    move=move.substr(16,2)+move.substr(19,2);
	    if(move=="    "){
		break;
	    }
	    status=validate_move(move);
	    if(!status){
		break;
	    }
	    moves.push_back(move);
	}
    }

    void ParsedScreen::print(){
	int i;
	for(i=0;i<4;i++){
	    printf("%s\n",header[i].c_str());
	}
	for(i=0;i<3;i++){
	    printf("%s\n",status_line[i].c_str());
	}
	for(i=0;i<moves.size();i++){
	    printf("%s\n",moves[i].c_str());
	}
    }

    class GameState {
    public:
	std::vector<std::string> moves;
	bool thinking;
	GameState();
    };

    GameState::GameState(){
	thinking=false;
    }

    GameState game_state;
	
    class KeyBoardBuffer {
	struct KeyEvent {
	    KBD_KEYS key;
	    bool pressed;
	};
	std::queue<KeyEvent> events;
	int last_ticks;
	void Push(KBD_KEYS k, bool pressed);
    public:
	KeyBoardBuffer();
	void SendKey(KBD_KEYS k);
	void SendKey(KBD_KEYS modifier, KBD_KEYS k);
	void ProcessKeys(int delay);
	bool Empty();
    };
    KeyBoardBuffer::KeyBoardBuffer(){
	    last_ticks=0;
    }
    void KeyBoardBuffer::Push(KBD_KEYS k, bool pressed){
	KeyEvent e;
	e.key=k;
	e.pressed=pressed;
	events.push(e);
    }
    void KeyBoardBuffer::SendKey(KBD_KEYS k){
	Push(k,true);
	Push(k,false);
    }
    // Key combination such as shift-4 ('$') or ctrl-L.
    void KeyBoardBuffer::SendKey(KBD_KEYS modifier, KBD_KEYS k){
	Push(modifier,true);
	Push(k,true);
	Push(k,false);
	Push(modifier,false);
    }
    bool KeyBoardBuffer::Empty(){
	return events.empty();
    }
    void KeyBoardBuffer::ProcessKeys(int delay=20){
	if(delay){
	    int new_ticks;
	    new_ticks=GetTicks();
	    if(new_ticks-last_ticks<delay){
		return;
	    }else{
		last_ticks=new_ticks;
	    }
	}
	if(!events.empty()){
	    KEYBOARD_AddKey(events.front().key,events.front().pressed);
	    events.pop();
	}
    }


    KeyBoardBuffer keyboard_buffer;

    void ResetCursor() {
	keyboard_buffer.SendKey(KBD_f3);
	keyboard_buffer.SendKey(KBD_f3);
    }

    void SendMove(bool white, int file1, int row1, int file2, int row2){
	int i;
	int file0,row0;
	//		printf("SendMove()\n");
	ResetCursor();
	file0=0;
	if(white){
	    row0=1;
	}else{
	    row0=6;
	}
	for(i=file0;i<file1;i++){
	    keyboard_buffer.SendKey(KBD_right);
	}
	if(row1>=row0){
	    for(i=row0;i<row1;i++){
		keyboard_buffer.SendKey(KBD_up);
	    }
	}else{
	    for(i=row0;i>row1;i--){
		keyboard_buffer.SendKey(KBD_down);
	    }
	}
	keyboard_buffer.SendKey(KBD_enter);
	//	ResetCursor();
	if(file1<=file2){
	    for(i=file1;i<file2;i++){
		keyboard_buffer.SendKey(KBD_right);
	    }
	}else{
	    for(i=file1;i>file2;i--){
		keyboard_buffer.SendKey(KBD_left);
	    }
	}
	if(row1<=row2){
	    for(i=row1;i<row2;i++){
		keyboard_buffer.SendKey(KBD_up);
	    }
	}else{
	    for(i=row1;i>row2;i--){
		keyboard_buffer.SendKey(KBD_down);
	    }
	}
	keyboard_buffer.SendKey(KBD_enter);
    }

    
    // The directory with Psion's files: $PSION_DIR, or "data" next to
    // the executable.
    static std::string data_dir(){
	const char *env=getenv("PSION_DIR");
	char exe[4096];
	if(env){
	    return env;
	}
#ifdef _WIN32
	DWORD n=GetModuleFileNameA(NULL,exe,sizeof(exe));
	if(n==0 || n>=sizeof(exe)){
	    return "data";
	}
	std::string path(exe,n);
	return path.substr(0,path.find_last_of("\\/"))+"\\data";
#else
	ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1);
	if(n<=0){
	    return "data";
	}
	exe[n]=0;
	std::string path(exe);
	return path.substr(0,path.rfind('/'))+"/data";
#endif
    }

    // Whether dir contains Chess.exe (in any case on Linux).
    static bool has_psion_files(const std::string &dir){
	static const char *names[]={"Chess.exe","CHESS.EXE","chess.exe"};
	size_t i;
	for(i=0;i<sizeof(names)/sizeof(names[0]);i++){
	    FILE *f=fopen((dir+"/"+names[i]).c_str(),"rb");
	    if(f){
		fclose(f);
		return true;
	    }
	}
	return false;
    }

    bool Psion_Init(DOS_Shell *d){
	char input_line[CMD_MAXLINE] = {0};
	char args[1];
	std::string dir=data_dir();
	if(!has_psion_files(dir)){
	    fprintf(stderr,"Psion Chess (Chess.exe etc.) not found in %s.\n"
		    "See data/README.md for where to get it, or set PSION_DIR.\n",
		    dir.c_str());
	    exit(1);
	}
	init_ocr();
	args[0]='0';
	snprintf(input_line,CMD_MAXLINE,"MOUNT C \"%s\"",dir.c_str());
	d->DoCommand(input_line);
	strcpy(input_line,"C:");
	d->DoCommand(input_line);
	strcpy(input_line,"CHESS.EXE");
	keyboard_buffer.SendKey(KBD_c);
	keyboard_buffer.SendKey(KBD_2);
	if(!(d->Execute(input_line,args))){
	    return false;
	}
	strcpy(input_line,"EXIT");
	d->DoCommand(input_line);
	return true;
    }
    
    static void start_input();

    void IO_Init(){
	setvbuf(stdin,NULL,_IONBF,0);
	setvbuf(stdout,NULL,_IONBF,0); 
#ifdef _WIN32
	timeBeginPeriod(1);  // makes Sleep() precise to 1 ms
	// Lines end in "\n", not "\r\n": GUIs on Windows accept that, and
	// GUIs elsewhere (e.g. running the engine under Wine) need it.
	_setmode(_fileno(stdout),_O_BINARY);
	_setmode(_fileno(stderr),_O_BINARY);
#endif
	start_input();
    }

#ifdef _WIN32
    // On Windows a thread reads stdin, so that the emulator never blocks
    // on input. (Checking a pipe for input without reading from it, as
    // the POSIX code below does, is unreliable there, e.g. under Wine.)
    static CRITICAL_SECTION input_lock;
    static std::queue<std::string> input_lines;
    static bool input_eof=false;

    static void add_input_line(const std::string &line){
	EnterCriticalSection(&input_lock);
	input_lines.push(line);
	LeaveCriticalSection(&input_lock);
    }

    // A console echoes what is typed relative to where the cursor was when
    // the read started, so output written while a read is pending (always,
    // with this thread) would make the echo jump back. Hence a console is
    // read key by key, and the thread echoes the keys itself.
    static void read_console(HANDLE in){
	HANDLE out=CreateFileA("CONOUT$",GENERIC_READ|GENERIC_WRITE,
			       FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
			       OPEN_EXISTING,0,NULL);
	std::string line;
	for(;;){
	    INPUT_RECORD event;
	    DWORD n;
	    if(!ReadConsoleInputA(in,&event,1,&n)){
		return;
	    }
	    if(n==0 || event.EventType!=KEY_EVENT ||
	       !event.Event.KeyEvent.bKeyDown){
		continue;
	    }
	    char c=event.Event.KeyEvent.uChar.AsciiChar;
	    const char *echo=NULL;
	    std::string typed;
	    if(event.Event.KeyEvent.wVirtualKeyCode==VK_RETURN ||
	       c=='\r' || c=='\n'){
		add_input_line(line+"\n");
		line.clear();
		echo="\r\n";
	    }else if(c=='\b' || c==127){
		if(line.empty()){
		    continue;
		}
		line.erase(line.size()-1);
		echo="\b \b";
	    }else if(c==26){  // Ctrl-Z: end of input
		return;
	    }else if((unsigned char)c>=32){
		line+=c;
		typed=std::string(1,c);
		echo=typed.c_str();
	    }else{
		continue;
	    }
	    if(out!=INVALID_HANDLE_VALUE){
		DWORD written;
		WriteConsoleA(out,echo,strlen(echo),&written,NULL);
	    }
	}
    }

    static DWORD WINAPI input_thread(LPVOID){
	HANDLE in=GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;
	if(GetConsoleMode(in,&mode)){
	    read_console(in);
	}else{
	    char line[65536];
	    while(fgets(line,sizeof(line),stdin)){
		add_input_line(line);
	    }
	}
	EnterCriticalSection(&input_lock);
	input_eof=true;
	LeaveCriticalSection(&input_lock);
	return 0;
    }

    static void start_input(){
	InitializeCriticalSection(&input_lock);
	CreateThread(NULL,0,input_thread,NULL,0,NULL);
    }

    // Returns true and a line of input if one is available; sets eof at
    // the end of the input.
    static bool read_input_line(std::string &line, bool &eof){
	bool ok=false;
	EnterCriticalSection(&input_lock);
	if(!input_lines.empty()){
	    line=input_lines.front();
	    input_lines.pop();
	    ok=true;
	}
	eof=!ok && input_eof;
	LeaveCriticalSection(&input_lock);
	return ok;
    }
#else
    static void start_input(){
    }

    /* Taken from fruit 2.1 source code */
    static bool input_available(){
	int val;
	fd_set set[1];
	struct timeval time_val[1];

	FD_ZERO(set);
	FD_SET(STDIN_FILENO,set);

	time_val->tv_sec = 0;
	time_val->tv_usec = 0;

	val = select(STDIN_FILENO+1,set,NULL,NULL,time_val);
	return val > 0;
    }

    // Returns true and a line of input if one is available; sets eof at
    // the end of the input.
    static bool read_input_line(std::string &line, bool &eof){
	char buf[65536];
	eof=false;
	if(!input_available()){
	    return false;
	}
	if(fgets(buf,sizeof(buf),stdin)){
	    line=buf;
	    return true;
	}
	eof=feof(stdin)!=0;
	return false;
    }
#endif

    static bool illegal_move_seen=false;

    // Debug mode: stay in 2-player mode and read moves for both sides
    // from stdin, so the engine never thinks.
    static bool two_player(){
	return getenv("PSION_TWOPLAYER")!=NULL;
    }

    // Offsets in Psion's data segment, found with memscan and find_state.py.
    const unsigned int MEM_BOARD=0x28AC;      // 64 bytes, a8..h8, a7..h7, ...
    const unsigned int MEM_SIDE=0x725B;       // 00 White, FF Black to move
    const unsigned int MEM_PLY=0x7260;        // word, counts the ponder move

    static bool read_state_at(unsigned int segment, PsionState &state){
	unsigned int base=segment*16;
	int f,r,white_kings=0,black_kings=0,unknown=0;
	unsigned char side;
	if(base+0x10000>MEM_TotalPages()*4096){
	    return false;
	}
	for(r=0;r<8;r++){
	    for(f=0;f<8;f++){
		unsigned char v=mem_readb(base+MEM_BOARD+(7-r)*8+f);
		char c=DecodeSquare(v);
		if((c=='P' || c=='p') && (r==0 || r==7)){
		    return false;
		}
		state.codes[f][r]=v;
		unknown+=c=='?';
		white_kings+=c=='K';
		black_kings+=c=='k';
		state.board[f][r]=c;
	    }
	}
	// A handful of unknown codes (promoted pieces) is resolved later;
	// many mean this is not Psion's board.
	if(white_kings!=1 || black_kings!=1 || unknown>2){
	    return false;
	}
	state.unknown=unknown;
	side=mem_readb(base+MEM_SIDE);
	if(side!=0x00 && side!=0xFF){
	    return false;
	}
	state.white_to_move=side==0x00;
	state.ply=mem_readw(base+MEM_PLY);
	if(state.ply>2000){
	    return false;
	}
	// While pondering, the predicted reply is included in the ply
	// counter but not played on the board.
	state.pondering=(state.ply%2==0)!=state.white_to_move;
	if(state.pondering){
	    state.ply--;
	}
	state.segment=segment;
	return true;
    }

    // Psion's data segment is located once, while Psion shows the start
    // position (it always does when the adapter starts it), and is never
    // changed after that: memory holds other board-like data that a later
    // search could mistake for the game.
    static bool is_initial_state(unsigned int segment, PsionState &state){
	return read_state_at(segment,state) && IsStartPosition(state.board) &&
	    state.ply==0;
    }

    bool ReadGameState(PsionState &state){
	static unsigned int segment=0;
	unsigned int s;
	if(segment){
	    return read_state_at(segment,state);
	}
	// DS is normally Psion's data segment when the screen is updated.
	s=SegValue(ds);
	if(s && is_initial_state(s,state)){
	    segment=s;
	}else{
	    // The full scan is expensive, so do not repeat it too often.
	    static unsigned int last_scan=0;
	    static bool scanned=false;
	    if(scanned && GetTicks()-last_scan<500){
		return false;
	    }
	    scanned=true;
	    last_scan=GetTicks();
	    for(s=1;s<0xA000 && !segment;s++){
		if(is_initial_state(s,state)){
		    segment=s;
		}
	    }
	    if(!segment){
		return false;
	    }
	}
	say("Psion's data segment is %04X\n",segment);
	return true;
    }

    // A memory state is accepted once it has been unchanged for this long;
    // while the engine searches, memory changes far more often.
    const unsigned int STABLE_MS=200;

    static PsionState known;          // last accepted state
    static bool have_known=false;

    static bool same_state(const PsionState &a, const PsionState &b){
	return a.ply==b.ply && a.white_to_move==b.white_to_move &&
	    memcmp(a.board,b.board,sizeof(a.board))==0;
    }

    static char ocr_board_buf[8][8];  // last OCR'd board

    // The piece typed at the last promotion prompt.
    static char last_promotion=0;
    static unsigned int last_promotion_ticks=0;
    static bool promotion_in_progress();

    // Guesses the pieces on squares with unknown codes (promoted pieces):
    // the piece just typed at a promotion prompt, or what the screen shows.
    static bool resolve_unknown_squares(PsionState &cur){
	int f,r;
	if(promotion_in_progress()){
	    return false;  // the board is in flux until a piece is chosen
	}
	for(f=0;f<8;f++){
	    for(r=0;r<8;r++){
		if(cur.board[f][r]!='?'){
		    continue;
		}
		bool white=!(cur.codes[f][r]&0x80);
		char c=ocr_board_buf[f][r];
		if(last_promotion && GetTicks()-last_promotion_ticks<5000 &&
		   (r==7 || r==0)){
		    c=white?toupper(last_promotion):last_promotion;
		}
		if(!c || !strchr("PNBRQKpnbrqk",c) || (isupper(c)!=0)!=white){
		    return false;
		}
		cur.board[f][r]=c;
	    }
	}
	return true;
    }

    static void learn_unknown_squares(const PsionState &cur){
	int f,r;
	for(f=0;f<8;f++){
	    for(r=0;r<8;r++){
		if(DecodeSquare(cur.codes[f][r])=='?'){
		    say("Learned square code %02X: %c\n",cur.codes[f][r],
			cur.board[f][r]);
		    LearnSquareCode(cur.codes[f][r],cur.board[f][r]);
		}
	    }
	}
    }

    // Track the game through Psion's memory. Returns true and sets move
    // when a new move has been played.
    static bool PollGameState(std::string &move){
	static PsionState pending;
	static bool have_pending=false;
	static unsigned int pending_since=0;
	PsionState cur;
	if(!ReadGameState(cur)){
	    // Psion flashes the kings to announce a draw or mate, which makes
	    // every other read fail. So ignore failed reads: the state read in
	    // between still has to stay unchanged for STABLE_MS.
	    return false;
	}
	if(!have_pending || !same_state(cur,pending)){
	    pending=cur;
	    pending_since=GetTicks();
	    have_pending=true;
	    return false;
	}
	if(GetTicks()-pending_since<STABLE_MS){
	    return false;
	}
	if(cur.unknown && !resolve_unknown_squares(cur)){
	    return false;
	}
	// While the engine searches its own move, the side byte (and hence
	// the corrected ply) is unreliable but the board is not. So track
	// the game through board changes, counting plies from the last state
	// read while the engine was idle.
	if(have_known &&
	   memcmp(cur.board,known.board,sizeof(cur.board))==0 &&
	   (game_state.thinking || (cur.ply==known.ply &&
				    cur.white_to_move==known.white_to_move))){
	    return false;
	}
	bool ok=have_known &&
	    DeriveMove(known.board,cur.board,known.white_to_move,move);
	if(!ok && cur.unknown){
	    return false;  // only trust a guessed piece if it explains a move
	}
	if(ok && cur.unknown){
	    learn_unknown_squares(cur);
	}
	if(ok){
	    cur.ply=known.ply+1;
	    cur.white_to_move=!known.white_to_move;
	}else if(have_known && game_state.thinking){
	    return false;  // transient state during the search
	}else{
	    say("Synchronized at ply %d, %s to move\n",cur.ply,
		   cur.white_to_move?"White":"Black");
	    if(cur.ply<=(int)game_state.moves.size()){
		game_state.moves.resize(cur.ply);
	    }else{
		game_state.moves.clear();
	    }
	}
	known=cur;
	have_known=true;
	return ok;
    }

    static bool white_to_move(){
	return have_known?known.white_to_move:game_state.moves.size()%2==0;
    }

    // Last non-blank contents of each screen row (some rows blink).
    static std::string screen_rows[TEXT_HEIGHT];

    // Row 6 names the players: "PLAYER PLAYER" in 2-player mode.
    static bool two_player_mode(){
	const std::string &r=screen_rows[6];
	return r.find("PLAYER")!=std::string::npos &&
	    r.find("PROGRAM")==std::string::npos;
    }

    // The level used: 4 minutes per move, longer than any search the
    // adapter asks for (MOVE-NOW ends it). Not "Infinite": at that level
    // Psion stops reading the keyboard once its search reaches the
    // maximum depth, which happens at once in a locked pawn ending.
    // "LEVEL 11  4 Min", or "LEVEL E 11  4 Min" in easy mode.
    static bool level_ok(){
	const std::string &r=screen_rows[22];
	return r.compare(0,6,"LEVEL ")==0 &&
	    r.find(" 11  4 Min")!=std::string::npos;
    }

    // In easy mode Psion does not ponder. (While pondering, it can hang
    // and stop reading the keyboard, e.g. in a locked pawn ending.)
    static bool easy_mode(){
	return screen_rows[22].compare(0,8,"LEVEL E ")==0;
    }

    // The piece to promote to, typed once Psion asks for it. Typed any
    // earlier, the letter would be taken as a command (Q is QUIT).
    static char pending_promotion=0;
    static unsigned int promotion_since=0;
    static bool promotion_prompt=false;  // "PROMOTE TO Q,R,B,N ?" shown
    const unsigned int PROMOTION_TIMEOUT_MS=3000;

    static void SendUciMove(const std::string &m){
	illegal_move_seen=false;
	SendMove(white_to_move(),m[0]-'a',m[1]-'1',m[2]-'a',m[3]-'1');
	pending_promotion=m.size()==5?m[4]:0;
	promotion_since=GetTicks();
    }

    static bool promotion_in_progress(){
	return pending_promotion || promotion_prompt;
    }

    static void SendPromotion(){
	if(!pending_promotion || !keyboard_buffer.Empty()){
	    return;
	}
	if(!promotion_prompt){
	    if(GetTicks()-promotion_since>PROMOTION_TIMEOUT_MS){
		say("No promotion prompt\n");
		pending_promotion=0;
	    }
	    return;
	}
	last_promotion=pending_promotion;
	last_promotion_ticks=GetTicks();
	switch(pending_promotion){
	case 'q': keyboard_buffer.SendKey(KBD_q); break;
	case 'r': keyboard_buffer.SendKey(KBD_r); break;
	case 'b': keyboard_buffer.SendKey(KBD_b); break;
	case 'n': keyboard_buffer.SendKey(KBD_n); break;
	}
	pending_promotion=0;
	promotion_prompt=false;
    }

    static void say_board(char board[8][8]){
	int f,r;
	for(r=7;r>=0;r--){
	    for(f=0;f<8;f++){
		say("%c",board[f][r]);
	    }
	    say("\n");
	}
    }

    /* UCI */

    // Time between the engine being told to move and its move being
    // detected (key delays plus STABLE_MS).
    const unsigned int MOVE_LATENCY_MS=250;
    // Pause after a key action before the screen is consulted again.
    const unsigned int ACTION_GAP_MS=300;
    const unsigned int MOVE_TIMEOUT_MS=5000;
    const int MAX_RETRIES=3;
    const int MAX_TWO_PLAYER_PRESSES=5;
    const unsigned int SYNC_TIMEOUT_MS=20000;

    enum SearchState {SEARCH_NONE, SEARCH_RUNNING, SEARCH_HOLDING};

    static struct {
	bool has_target;              // a position has been given
	bool unsupported;             // ... but we cannot set it up
	std::vector<std::string> target;
	bool new_game;                // ucinewgame: press '$' before setting
				      // up the next position
	bool readyok_pending;
	bool go_pending;
	SearchState search;
	bool infinite;                // wait for "stop" before bestmove
	bool stop_received;
	unsigned int think_ms;        // 0: until "stop"
	unsigned int search_start;    // when "go" was received
	bool movenow_sent;
	unsigned int movenow_ticks;
	std::string held_move;        // found before "stop" in infinite mode
	unsigned int last_action;
	bool inflight;                // a position move has been sent
	size_t inflight_size;
	unsigned int inflight_since;
	int retries;
	bool level_set;
	int two_player_presses;       // '2' pressed without effect
	int easy_presses;             // 'e' pressed without effect
	size_t sync_progress_size;    // moves played when last checked
	unsigned int sync_progress_ticks;
    } uci;

    static void uci_send(const char *format, ...){
	va_list args;
	va_start(args,format);
	vfprintf(stdout,format,args);
	va_end(args);
	fflush(stdout);
    }

    static void press(KBD_KEYS k){
	keyboard_buffer.SendKey(k);
	uci.last_action=GetTicks();
    }

    static void press(KBD_KEYS modifier, KBD_KEYS k){
	keyboard_buffer.SendKey(modifier,k);
	uci.last_action=GetTicks();
    }

    static bool uci_synced(){
	return !uci.has_target || uci.unsupported ||
	    game_state.moves==uci.target;
    }

    static void finish_search(const std::string &move){
	uci.search=SEARCH_NONE;
	game_state.thinking=false;
	uci_send("bestmove %s\n",move.c_str());
    }

    // Called when Psion plays a move during a search.
    static void UciEngineMoved(const std::string &move){
	if(uci.search!=SEARCH_RUNNING){
	    return;
	}
	game_state.thinking=false;
	press(KBD_2);  // back to 2-player mode
	if(uci.has_target && !uci.unsupported){
	    uci.target.push_back(move);
	}
	if(uci.infinite && !uci.stop_received){
	    uci.held_move=move;
	    uci.search=SEARCH_HOLDING;
	}else{
	    finish_search(move);
	}
    }

    static void UciGo(const std::vector<std::string> &tokens){
	GoParams go=ParseGo(tokens);
	if(uci.has_target && uci.unsupported){
	    uci_send("bestmove 0000\n");
	    return;
	}
	// The side to move once the position has been set up.
	bool white=(uci.has_target?uci.target.size():game_state.moves.size())%2==0;
	uci.infinite=go.infinite;
	uci.think_ms=ThinkTime(go,white,MOVE_LATENCY_MS);
	uci.stop_received=false;
	uci.go_pending=true;
	// The GUI's clock runs from now, including the time it takes to set
	// up the position.
	uci.search_start=GetTicks();
    }

    static void UciPosition(const std::vector<std::string> &tokens){
	PositionRequest req=ParsePosition(tokens);
	uci.sync_progress_size=game_state.moves.size();
	uci.sync_progress_ticks=GetTicks();
	uci.has_target=true;
	uci.unsupported=!req.supported;
	uci.target=req.moves;
	uci.retries=0;
	if(!req.supported){
	    uci_send("info string %s\n",req.error.c_str());
	}
    }

    static void UciCommand(const char *line){
	std::vector<std::string> tokens=Tokenize(line);
	if(tokens.empty()){
	    return;
	}
	const std::string &cmd=tokens[0];
	if(cmd=="uci"){
	    uci_mode=true;
	    uci_send("id name Psion Chess 1985\n");
	    uci_send("id author Richard Lang\n");
	    uci_send("uciok\n");
	}else if(cmd=="isready"){
	    if(uci.search!=SEARCH_NONE){
		uci_send("readyok\n");
	    }else{
		uci.readyok_pending=true;
	    }
	}else if(cmd=="ucinewgame"){
	    uci.new_game=true;
	}else if(cmd=="position"){
	    if(uci.search==SEARCH_NONE){
		UciPosition(tokens);
	    }
	}else if(cmd=="go"){
	    if(uci.search==SEARCH_NONE){
		UciGo(tokens);
	    }
	}else if(cmd=="stop"){
	    uci.stop_received=true;
	    uci.go_pending=false;
	    if(uci.search==SEARCH_HOLDING){
		finish_search(uci.held_move);
	    }else if(uci.search==SEARCH_RUNNING && !uci.movenow_sent){
		press(KBD_m);
		uci.movenow_sent=true;
		uci.movenow_ticks=GetTicks();
	    }
	}else if(cmd=="debug"){
	    if(tokens.size()>1){
		verbose=tokens[1]=="on";
	    }
	}else if(cmd=="quit"){
	    exit(0);
	}
    }

    // Sets the level (see level_ok()).
    // Returns true once that is done.
    static bool level_set_check(){
	if(uci.level_set){
	    return true;
	}
	if(level_ok()){
	    uci.level_set=true;
	    return true;
	}
	if(screen_rows[22].find("LEVEL")!=std::string::npos){
	    press(KBD_l);
	}
	return false;
    }

    // Switches easy mode on. Returns true once that is done, or when it
    // does not work.
    const int MAX_EASY_PRESSES=4;
    static bool easy_check(){
	if(easy_mode()){
	    uci.easy_presses=0;
	    return true;
	}
	if(uci.easy_presses>=MAX_EASY_PRESSES){
	    return true;
	}
	uci.easy_presses++;
	press(KBD_e);
	return false;
    }

    // Drives Psion towards the state the GUI asked for. Called every tick.
    static void UciStep(){
	unsigned int now=GetTicks();
	bool idle=keyboard_buffer.Empty() && now-uci.last_action>=ACTION_GAP_MS;
	std::vector<std::string> &moves=game_state.moves;
	if(uci.search==SEARCH_RUNNING){
	    if(!uci.movenow_sent && uci.think_ms &&
	       now-uci.search_start>=uci.think_ms){
		press(KBD_m);
		uci.movenow_sent=true;
		uci.movenow_ticks=now;
	    }else if(uci.movenow_sent && now-uci.movenow_ticks>MOVE_TIMEOUT_MS){
		say("Psion did not move\n");
		press(KBD_2);
		finish_search("0000");
	    }
	    return;
	}
	if(uci.search==SEARCH_HOLDING || !have_known || !idle ||
	   pending_promotion){
	    return;
	}
	if(!level_set_check() || !easy_check()){
	    return;
	}
	if(!uci_synced()){
	    // Give up if setting up the position makes no progress, rather
	    // than leave the GUI waiting for readyok.
	    if(moves.size()!=uci.sync_progress_size){
		uci.sync_progress_size=moves.size();
		uci.sync_progress_ticks=now;
	    }else if(now-uci.sync_progress_ticks>SYNC_TIMEOUT_MS){
		uci_send("info string Cannot set up the position\n");
		uci.unsupported=true;
		return;
	    }
	    if(uci.new_game){
		uci.new_game=false;
		if(!moves.empty()){
		    press(KBD_leftshift,KBD_4);   // '$': new game
		    return;
		}
	    }
	    if(!two_player_mode()){
		// Once Psion considers the game over (e.g. it announced a
		// draw) it ignores '2'; only a new game helps then.
		if(++uci.two_player_presses>MAX_TWO_PLAYER_PRESSES){
		    say("Psion does not switch to 2-player mode, starting a new game\n");
		    uci.two_player_presses=0;
		    press(KBD_leftshift,KBD_4);
		}else{
		    press(KBD_2);
		}
		return;
	    }
	    uci.two_player_presses=0;
	    if(uci.inflight){
		if(moves.size()!=uci.inflight_size){
		    uci.inflight=false;
		    uci.retries=0;
		}else if(illegal_move_seen ||
			 now-uci.inflight_since>MOVE_TIMEOUT_MS){
		    uci.inflight=false;
		    if(++uci.retries>MAX_RETRIES){
			uci_send("info string Psion does not accept %s\n",
				 uci.target[moves.size()].c_str());
			uci.unsupported=true;
		    }
		}
		return;
	    }
	    switch(PlanSync(moves,uci.target)){
	    case SYNC_SEND_MOVE:
		SendUciMove(uci.target[moves.size()]);
		uci.last_action=now;
		uci.inflight=true;
		uci.inflight_size=moves.size();
		uci.inflight_since=now;
		break;
	    case SYNC_TAKEBACK:
		press(KBD_t);
		break;
	    case SYNC_NEW_GAME:
		press(KBD_leftshift,KBD_4);
		break;
	    case SYNC_DONE:
		break;
	    }
	    return;
	}
	if(uci.readyok_pending){
	    uci.readyok_pending=false;
	    uci_send("readyok\n");
	}
	if(uci.go_pending){
	    uci.go_pending=false;
	    press(KBD_c);
	    game_state.thinking=true;
	    uci.search=SEARCH_RUNNING;
	    uci.movenow_sent=false;
	}
    }

    void ProcessScreen(unsigned char pixels[SCREEN_BPP*SCREEN_WIDTH*SCREEN_HEIGHT]){
	char (&board)[8][8]=ocr_board_buf;
	char txt_buf[TEXT_HEIGHT][TEXT_WIDTH];
	unsigned int i;
	ocr(txt_buf,(unsigned char *)pixels);
	ParsedScreen p=ParsedScreen(txt_buf);
	for(i=0;i<(unsigned int)TEXT_HEIGHT;i++){
	    std::string row(txt_buf[i],TEXT_WIDTH);
	    if(row.find_first_not_of(' ')!=std::string::npos){
		screen_rows[i]=row;
	    }
	}
	ocr_board(board,(unsigned char *)pixels);
	if(MemScan_Enabled()){
	    // memscan uses the screen as ground truth
	    static bool start_snapshot_taken=false;
	    if(!start_snapshot_taken && game_state.moves.empty() &&
	       IsStartPosition(board)){
		start_snapshot_taken=true;
		MemScan_Snapshot(board);
	    }
	}
	promotion_prompt=p.status_line[2].find("PROMOTE TO")!=std::string::npos;
	if(!illegal_move_seen && p.status_line[2].find("ILLEGAL")!=std::string::npos){
	    say("Illegal move!\n");
	    illegal_move_seen=true;
	    ResetCursor();
	}
    }

    // Polls Psion's memory for new moves. The screen is not redrawn when
    // nothing changes, so this runs from the event loop instead.
    static void CheckGameState(){
	static unsigned int last_poll=0;
	std::string move;
	unsigned int i;
	if(GetTicks()-last_poll<10){
	    return;
	}
	last_poll=GetTicks();
	if(!PollGameState(move)){
	    return;
	}
	illegal_move_seen=false;
	game_state.moves.push_back(move);
	for(i=0;i<game_state.moves.size();i++){
	    say("%s ",game_state.moves[i].c_str());
	}
	say("\n");
	say_board(known.board);
	say("Ply %d, %s to move%s\n",known.ply,
	    known.white_to_move?"White":"Black",
	    known.pondering?", pondering":"");
	if(MemScan_Enabled()){
	    MemScan_Snapshot(ocr_board_buf);
	}
	if(uci_mode){
	    UciEngineMoved(move);
	    return;
	}
	if(two_player()){
	    return;
	}
	// The user plays White.
	game_state.thinking=!known.white_to_move;
	if(game_state.thinking){
	    say("Thinking!\n");
	    keyboard_buffer.SendKey(KBD_c);
	}else{
	    say("Stopped thinking!\n");
	    keyboard_buffer.SendKey(KBD_2);
	}
    }

    void ProcessConsole(){
	std::string line;
	bool eof=false;
	// In the interactive mode, moves are only read on the user's turn.
	if((uci_mode || !game_state.thinking) && read_input_line(line,eof)){
	    std::string move(line);
	    move.erase(move.find_last_not_of(" \r\n")+1);
	    if(uci_mode || line.compare(0,3,"uci")==0){
		UciCommand(line.c_str());
	    }else if(IsMoveString(move)){
		SendUciMove(move);
	    }
	}else if(eof && uci_mode){
	    exit(0);
	}
	CheckGameState();
	SendPromotion();
	if(uci_mode){
	    UciStep();
	}
	keyboard_buffer.ProcessKeys();
    }

}
