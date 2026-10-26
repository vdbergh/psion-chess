#include <limits.h>
// Unit tests for src/psion_mods/chess.cpp. Run with "make check".

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "chess.h"

using namespace psion;

static int checks=0, failures=0;

#define CHECK(cond) do {                                              \
	checks++;                                                     \
	if(!(cond)){                                                  \
	    failures++;                                               \
	    printf("%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#cond); \
	}                                                             \
    } while(0)

#define CHECK_EQ(a,b) do {                                            \
	checks++;                                                     \
	if(!((a)==(b))){                                              \
	    failures++;                                               \
	    printf("%s:%d: CHECK_EQ failed: %s == %s\n",              \
		   __FILE__,__LINE__,#a,#b);                          \
	}                                                             \
    } while(0)

// Builds a board from a diagram, rank 8 first.
static void make_board(Board board, const char *diagram){
    int f,r;
    CHECK_EQ(strlen(diagram),(size_t)64);
    for(r=0;r<8;r++){
	for(f=0;f<8;f++){
	    board[f][7-r]=diagram[8*r+f];
	}
    }
}

static std::vector<std::string> split(const char *line){
    return Tokenize(line);
}

// Derives the move between two diagrams; "" if there is none.
static std::string derive(const char *before, const char *after, bool white){
    Board b,a;
    std::string move;
    make_board(b,before);
    make_board(a,after);
    if(!DeriveMove(b,a,white,move)){
	return "";
    }
    return move;
}

static const char *START=
    "rnbqkbnr"
    "pppppppp"
    "........"
    "........"
    "........"
    "........"
    "PPPPPPPP"
    "RNBQKBNR";

static void test_decode_square(){
    CHECK_EQ(DecodeSquare(0x00),'.');
    CHECK_EQ(DecodeSquare(0x2C),'P');
    CHECK_EQ(DecodeSquare(0x24),'N');
    CHECK_EQ(DecodeSquare(0x1C),'B');
    CHECK_EQ(DecodeSquare(0x0A),'R');
    CHECK_EQ(DecodeSquare(0x17),'Q');
    CHECK_EQ(DecodeSquare(0x01),'K');
    CHECK_EQ(DecodeSquare(0xAC),'p');
    CHECK_EQ(DecodeSquare(0xA4),'n');
    CHECK_EQ(DecodeSquare(0xDC),'b');
    CHECK_EQ(DecodeSquare(0x8A),'r');
    CHECK_EQ(DecodeSquare(0x97),'q');
    CHECK_EQ(DecodeSquare(0xC1),'k');
    // codes seen after pieces moved or castled
    CHECK_EQ(DecodeSquare(0x64),'N');
    CHECK_EQ(DecodeSquare(0xEC),'p');
    CHECK_EQ(DecodeSquare(0x4A),'R');
    CHECK_EQ(DecodeSquare(0x0E),'R');
    CHECK_EQ(DecodeSquare(0x57),'Q');
    CHECK_EQ(DecodeSquare(0x45),'K');
    // unknown codes
    CHECK_EQ(DecodeSquare(0x02),'?');
    CHECK_EQ(DecodeSquare(0xFF),'?');
    CHECK_EQ(DecodeSquare(0x80),'?');
}

static void test_learn_square_code(){
    // a promoted queen seen in a game
    CHECK_EQ(DecodeSquare(0x10),'?');
    LearnSquareCode(0x10,'Q');
    CHECK_EQ(DecodeSquare(0x10),'Q');
    CHECK_EQ(DecodeSquare(0x90),'?');
    // learning does not override the fixed codes
    LearnSquareCode(0x2C,'Q');
    CHECK_EQ(DecodeSquare(0x2C),'P');
}

static void test_is_start_position(){
    Board b;
    make_board(b,START);
    CHECK(IsStartPosition(b));
    b[4][1]='.';
    b[4][3]='P';
    CHECK(!IsStartPosition(b));
    make_board(b,START);
    b[0][0]='.';
    CHECK(!IsStartPosition(b));
}

static void test_derive_simple_moves(){
    const char *e4=
	"rnbqkbnr"
	"pppppppp"
	"........"
	"........"
	"....P..."
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *e4e5=
	"rnbqkbnr"
	"pppp.ppp"
	"........"
	"....p..."
	"....P..."
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *nf3=
	"rnbqkbnr"
	"pppppppp"
	"........"
	"........"
	"........"
	".....N.."
	"PPPPPPPP"
	"RNBQKB.R";
    CHECK_EQ(derive(START,e4,true),std::string("e2e4"));
    CHECK_EQ(derive(e4,e4e5,false),std::string("e7e5"));
    CHECK_EQ(derive(START,nf3,true),std::string("g1f3"));
    // the wrong side to move
    CHECK_EQ(derive(START,e4,false),std::string(""));
    CHECK_EQ(derive(e4,e4e5,true),std::string(""));
    // no change
    CHECK_EQ(derive(START,START,true),std::string(""));
}

static void test_derive_capture(){
    const char *before=
	"rnbqkbnr"
	"ppp.pppp"
	"........"
	"...p...."
	"....P..."
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *after=
	"rnbqkbnr"
	"ppp.pppp"
	"........"
	"...P...."
	"........"
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    CHECK_EQ(derive(before,after,true),std::string("e4d5"));
}

static void test_derive_castling(){
    const char *before=
	"r...k..r"
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPP"
	"R...K..R";
    const char *white_short=
	"r...k..r"
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPP"
	"R....RK.";
    const char *white_long=
	"r...k..r"
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPP"
	"..KR...R";
    const char *black_short=
	"r....rk."
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPP"
	"R...K..R";
    const char *black_long=
	"..kr...r"
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPP"
	"R...K..R";
    CHECK_EQ(derive(before,white_short,true),std::string("e1g1"));
    CHECK_EQ(derive(before,white_long,true),std::string("e1c1"));
    CHECK_EQ(derive(before,black_short,false),std::string("e8g8"));
    CHECK_EQ(derive(before,black_long,false),std::string("e8c8"));
    // while Psion updates its board: the king has moved, the rook not yet
    const char *white_short_half=
	"r...k..r"
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPP"
	"R.....KR";
    CHECK_EQ(derive(before,white_short_half,true),std::string(""));
}

static void test_derive_en_passant(){
    const char *white_before=
	"rnbqkbnr"
	"ppppp.pp"
	"........"
	"....Pp.."
	"........"
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *white_after=
	"rnbqkbnr"
	"ppppp.pp"
	".....P.."
	"........"
	"........"
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *black_before=
	"rnbqkbnr"
	"ppp.pppp"
	"........"
	"........"
	"...pP..."
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *black_after=
	"rnbqkbnr"
	"ppp.pppp"
	"........"
	"........"
	"........"
	"....p..."
	"PPPP.PPP"
	"RNBQKBNR";
    CHECK_EQ(derive(white_before,white_after,true),std::string("e5f6"));
    CHECK_EQ(derive(black_before,black_after,false),std::string("d4e3"));
    // the pawn taken has not been removed yet
    const char *white_half=
	"rnbqkbnr"
	"ppppp.pp"
	".....P.."
	".....p.."
	"........"
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    CHECK_EQ(derive(white_before,white_half,true),std::string(""));
}

static void test_derive_promotion(){
    const char *before=
	"rnbqkbr."
	"pppppp.P"
	"........"
	"........"
	"........"
	"........"
	"PPPPPPP."
	"RNBQKBNR";
    const char *queen=
	"rnbqkbrQ"
	"pppppp.."
	"........"
	"........"
	"........"
	"........"
	"PPPPPPP."
	"RNBQKBNR";
    const char *knight_capture=
	"rnbqkbN."
	"pppppp.."
	"........"
	"........"
	"........"
	"........"
	"PPPPPPP."
	"RNBQKBNR";
    const char *black_before=
	"rnbqkbnr"
	"ppppppp."
	"........"
	"........"
	"........"
	"........"
	"PPPPPPPp"
	"RNBQKB.."
	;
    const char *black_rook=
	"rnbqkbnr"
	"ppppppp."
	"........"
	"........"
	"........"
	"........"
	"PPPPPPP."
	"RNBQKB.r";
    CHECK_EQ(derive(before,queen,true),std::string("h7h8q"));
    CHECK_EQ(derive(before,knight_capture,true),std::string("h7g8n"));
    CHECK_EQ(derive(black_before,black_rook,false),std::string("h2h1r"));
}

static void test_derive_rejects_impossible_changes(){
    const char *two_moves=
	"rnbqkbnr"
	"pppppppp"
	"........"
	"........"
	"...PP..."
	"........"
	"PPP..PPP"
	"RNBQKBNR";
    const char *piece_vanished=
	"rnbqkbnr"
	"pppppppp"
	"........"
	"........"
	"........"
	"........"
	"PPPP.PPP"
	"RNBQKBNR";
    const char *opponent_appeared=
	"rnbqkbnr"
	"pppppppp"
	"........"
	"........"
	"....P..."
	"....n..."
	"PPPP.PPP"
	"RNBQKBNR";
    const char *two_pieces_no_king=
	"rnbqkbnr"
	"pppppppp"
	"........"
	"........"
	"........"
	"..N..N.."
	"PPPPPPPP"
	"R.BQKB.R";
    // A knight move that also removes a black pawn is not en passant.
    const char *fake_en_passant_before=
	"rnbqkbnr"
	"pppp.ppp"
	"........"
	"....p..."
	"........"
	"........"
	"PPPPPPPP"
	"RNBQKBNR";
    const char *fake_en_passant_after=
	"rnbqkbnr"
	"pppp.ppp"
	"........"
	"........"
	"........"
	".....N.."
	"PPPPPPPP"
	"RNBQKB.R";
    CHECK_EQ(derive(START,two_moves,true),std::string(""));
    CHECK_EQ(derive(START,piece_vanished,true),std::string(""));
    CHECK_EQ(derive(START,opponent_appeared,true),std::string(""));
    CHECK_EQ(derive(START,two_pieces_no_king,true),std::string(""));
    CHECK_EQ(derive(fake_en_passant_before,fake_en_passant_after,true),
	     std::string(""));
}

static void test_is_move_string(){
    CHECK(IsMoveString("e2e4"));
    CHECK(IsMoveString("a1h8"));
    CHECK(IsMoveString("a7a8q"));
    CHECK(IsMoveString("b2b1n"));
    CHECK(!IsMoveString(""));
    CHECK(!IsMoveString("e2"));
    CHECK(!IsMoveString("e2e9"));
    CHECK(!IsMoveString("i2e4"));
    CHECK(!IsMoveString("E2E4"));
    CHECK(!IsMoveString("e2e4k"));
    CHECK(!IsMoveString("e2e4qq"));
    CHECK(!IsMoveString("0000"));
}

static void test_tokenize(){
    std::vector<std::string> t=Tokenize("  go \t wtime  1000\r\n");
    CHECK_EQ(t.size(),(size_t)3);
    if(t.size()==3){
	CHECK_EQ(t[0],std::string("go"));
	CHECK_EQ(t[1],std::string("wtime"));
	CHECK_EQ(t[2],std::string("1000"));
    }
    CHECK(Tokenize("").empty());
    CHECK(Tokenize(" \n").empty());
}

static void test_parse_go(){
    GoParams go=ParseGo(split("go"));
    CHECK(!go.wtime_given);
    CHECK(!go.btime_given);
    CHECK_EQ(go.winc,0);
    CHECK_EQ(go.movestogo,0);
    CHECK_EQ(go.movetime,0);
    CHECK(!go.infinite);
    go=ParseGo(split("go wtime 60000 btime 50000 winc 1000 binc 2000 movestogo 20"));
    CHECK_EQ(go.wtime,60000);
    CHECK_EQ(go.btime,50000);
    CHECK_EQ(go.winc,1000);
    CHECK_EQ(go.binc,2000);
    CHECK_EQ(go.movestogo,20);
    CHECK(go.wtime_given);
    CHECK(go.btime_given);
    // clamped to 0 .. INT_MAX
    go=ParseGo(split("go wtime -20 btime 99999999999 winc -5 binc x"));
    CHECK(go.wtime_given);
    CHECK_EQ(go.wtime,0);
    CHECK_EQ(go.btime,INT_MAX);
    CHECK_EQ(go.winc,0);
    CHECK_EQ(go.binc,0);
    go=ParseGo(split("go movetime 1500"));
    CHECK_EQ(go.movetime,1500);
    CHECK(ParseGo(split("go infinite")).infinite);
    go=ParseGo(split("go ponder wtime 30000 btime 1000"));
    CHECK(go.ponder);
    CHECK(!go.infinite);
    // the time for the move, once "ponderhit" comes (sudden death: /60)
    CHECK_EQ(ThinkTime(go,true,0),500u);
    // a value missing at the end
    CHECK_EQ(ParseGo(split("go wtime")).wtime,0);
}

static void test_think_time(){
    GoParams go=ParseGo(split("go movetime 2000"));
    CHECK_EQ(ThinkTime(go,true,400),1600u);
    CHECK_EQ(ThinkTime(go,false,400),1600u);
    // 60000/30 + 3/4 of the increment
    go=ParseGo(split("go wtime 60000 btime 30000 winc 1000 binc 0"));
    CHECK_EQ(ThinkTime(go,true,400),2350u);
    // sudden death: 30000/60
    CHECK_EQ(ThinkTime(go,false,400),100u);
    go=ParseGo(split("go wtime 60000 btime 60000"));
    CHECK_EQ(ThinkTime(go,true,94),906u);
    // movestogo
    go=ParseGo(split("go wtime 60000 btime 60000 movestogo 20"));
    CHECK_EQ(ThinkTime(go,true,94),2906u);
    // at most 3/4 of the remaining time
    go=ParseGo(split("go wtime 10000 btime 10000 movestogo 1"));
    CHECK_EQ(ThinkTime(go,true,400),7100u);
    // never less than 50 ms
    go=ParseGo(split("go wtime 100 btime 100"));
    CHECK_EQ(ThinkTime(go,true,400),50u);
    // the time used up (negative or 0): the minimum, not the default
    go=ParseGo(split("go wtime -20 btime 0"));
    CHECK_EQ(ThinkTime(go,true,94),50u);
    CHECK_EQ(ThinkTime(go,false,94),50u);
    // huge clocks and increments do not overflow
    go=ParseGo(split("go wtime 2147483647 btime 2147483647 winc 2147483647 binc 0"));
    CHECK_EQ(ThinkTime(go,true,0),(unsigned int)(3*2147483647LL/4)); // the 3/4 cap
    CHECK_EQ(ThinkTime(go,false,0),(unsigned int)(2147483647LL/60));
    go=ParseGo(split("go wtime 99999999999 btime 1"));
    CHECK_EQ(ThinkTime(go,true,0),(unsigned int)(2147483647LL/60));
    // no time information: a fixed default
    go=ParseGo(split("go depth 10"));
    CHECK_EQ(ThinkTime(go,true,400),4600u);
    go=ParseGo(split("go infinite"));
    CHECK_EQ(ThinkTime(go,true,400),0u);
}

static void test_cycles_latency(){
    CHECK_EQ(CyclesLatency(0),0u);
    CHECK_EQ(CyclesLatency(100000),0u);
    CHECK_EQ(CyclesLatency(500000),0u);
    // measured speeds
    CHECK_EQ(CyclesLatency(300),5144u);
    CHECK_EQ(CyclesLatency(3000),100u);
    // in between, linear in log(cycles)
    CHECK(CyclesLatency(2200)<297u && CyclesLatency(2200)>159u);
    // below 300: proportional to 1/cycles
    CHECK_EQ(CyclesLatency(150),10288u);
    // decreasing
    unsigned int prev=CyclesLatency(1);
    for(unsigned int c=2;c<=100000;c=c*3/2+1){
        CHECK(CyclesLatency(c)<=prev);
        prev=CyclesLatency(c);
    }
}

static void test_elo_cycles(){
    CHECK_EQ(EloCycles(1614),100000u);
    CHECK_EQ(EloCycles(1614-86),50000u);
    // the range
    CHECK_EQ(EloCycles(MIN_ELO),2376u);
    CHECK_EQ(EloCycles(MAX_ELO),133663u);
    CHECK_EQ(EloCycles(0),EloCycles(MIN_ELO));
    CHECK_EQ(EloCycles(3000),EloCycles(MAX_ELO));
    // increasing
    for(int e=MIN_ELO;e<MAX_ELO;e++){
        CHECK(EloCycles(e)<EloCycles(e+1));
    }
}

static void test_set_option(){
    std::string name,value;
    CHECK(ParseSetOption(split("setoption name Move Overhead value 270"),name,value));
    CHECK_EQ(name,std::string("Move Overhead"));
    CHECK_EQ(value,std::string("270"));
    CHECK(ParseSetOption(split("setoption name Ponder"),name,value));
    CHECK_EQ(name,std::string("Ponder"));
    CHECK_EQ(value,std::string(""));
    CHECK(!ParseSetOption(split("setoption value 3"),name,value));
    CHECK(SameOptionName("move overhead","Move Overhead"));
    CHECK(!SameOptionName("Move Overheads","Move Overhead"));
}

static void test_parse_position(){
    PositionRequest req=ParsePosition(split("position startpos"));
    CHECK(req.supported);
    CHECK(req.moves.empty());
    req=ParsePosition(split("position startpos moves e2e4 e7e5 e7e8q"));
    CHECK(req.supported);
    CHECK_EQ(req.moves.size(),(size_t)3);
    if(req.moves.size()==3){
	CHECK_EQ(req.moves[0],std::string("e2e4"));
	CHECK_EQ(req.moves[2],std::string("e7e8q"));
    }
    req=ParsePosition(split("position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves d2d4"));
    CHECK(req.supported);
    CHECK_EQ(req.moves.size(),(size_t)1);
    CHECK(req.start.standard);
    req=ParsePosition(split("position startpos moves e2e4 junk"));
    CHECK(!req.supported);
    CHECK(req.moves.empty());
    CHECK_EQ(req.error,std::string("Invalid move junk"));
    req=ParsePosition(split("position"));
    CHECK(!req.supported);
    req=ParsePosition(split("position nonsense"));
    CHECK(!req.supported);
}

static void test_parse_fen(){
    // A FEN is set up (also the start position without castling rights).
    PositionRequest req=ParsePosition(split("position fen 8/8/8/4k3/8/8/4K3/8 w - - 0 1 moves e2d3"));
    CHECK(req.supported);
    CHECK(!req.start.standard);
    CHECK(req.start.white);
    CHECK_EQ(req.start.board[4][1],'K');
    CHECK_EQ(req.start.board[4][4],'k');
    CHECK_EQ(req.moves.size(),(size_t)1);
    req=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/4K3 b - -"));
    CHECK(req.supported);
    CHECK(!req.start.white);
    req=ParsePosition(split("position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1"));
    CHECK(req.supported);
    CHECK(!req.start.standard);
    CHECK_EQ(req.start.castling,std::string("-"));
    // Castling rights
    req=ParsePosition(split("position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    CHECK(req.supported);
    CHECK(req.start.standard);
    req=ParsePosition(split("position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w kKq - 0 1"));
    CHECK(req.supported);
    CHECK(!req.start.standard);
    CHECK_EQ(req.start.castling,std::string("Kkq"));
    req=ParsePosition(split("position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(req.supported);
    CHECK_EQ(req.start.castling,std::string("KQkq"));
    req=ParsePosition(split("position fen r3k2r/8/8/8/8/8/8/R3K2R b k - 0 1"));
    CHECK(req.supported);
    CHECK_EQ(req.start.castling,std::string("k"));
    // ... without those whose king or rook is not on its square
    req=ParsePosition(split("position fen r3k3/8/8/8/8/8/8/R4K1R w KQkq - 0 1"));
    CHECK(req.supported);
    CHECK_EQ(req.start.castling,std::string("q"));
    req=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1"));
    CHECK(req.supported);
    CHECK_EQ(req.start.castling,std::string("-"));
    // Chess960 (Shredder-FEN) castling rights
    req=ParsePosition(split("position fen r3k2r/8/8/8/8/8/8/R3K2R w HAha - 0 1"));
    CHECK(!req.supported);
    CHECK_EQ(req.error,std::string("Chess960 castling rights are not supported"));
    // En passant: set up before the pawn moved two squares.
    req=ParsePosition(split("position fen 4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1 moves e5d6"));
    CHECK(req.supported);
    CHECK(!req.start.white);
    CHECK_EQ(req.start.board[3][6],'p');
    CHECK_EQ(req.start.board[3][4],'.');
    CHECK_EQ(req.moves.size(),(size_t)2);
    if(req.moves.size()==2){
	CHECK_EQ(req.moves[0],std::string("d7d5"));
	CHECK_EQ(req.moves[1],std::string("e5d6"));
    }
    req=ParsePosition(split("position fen 4k3/8/8/8/2pP4/8/8/4K3 b - d3 0 1"));
    CHECK(req.supported);
    CHECK(req.start.white);
    CHECK_EQ(req.start.board[3][1],'P');
    CHECK_EQ(req.moves.size(),(size_t)1);
    if(req.moves.size()==1){
	CHECK_EQ(req.moves[0],std::string("d2d4"));
    }
    // ... unless no pawn can take: as if not given
    req=ParsePosition(split("position fen 4k3/8/8/3p4/8/8/8/4K3 w - d6 0 1"));
    CHECK(req.supported);
    CHECK(req.start.white);
    CHECK_EQ(req.start.board[3][4],'p');
    CHECK(req.moves.empty());
    // Invalid FENs
    const char *invalid[]={
	"4k3/8/8/8/8/8/8/4K3 w - e3 0 1",   // no pawn moved two squares
	"8/8/8/8/8/8/8/4K3 w - - 0 1",      // no black king
	"4k3/8/8/8/8/8/8/4K2r b - - 0 1",   // the side not to move is in check
	"P3k3/8/8/8/8/8/8/4K3 w - - 0 1",   // a pawn on the eighth rank
	"4k3/8/8/8/8/8/8/4K3 x - - 0 1",
	"4k3/8/8/8/8/8/8/4K4 w - - 0 1",
	"4k3/8/8/8/8/8/4K3 w - - 0 1",
	"4k3/8/8/8/8/8/8/4K3 w",
	"4k3/8/8/8/8/8/8/4K3 w X - 0 1",
	0};
    for(const char **f=invalid;*f;f++){
	req=ParsePosition(split((std::string("position fen ")+*f+" moves e2e4").c_str()));
	CHECK(!req.supported);
	CHECK_EQ(req.error,std::string("Invalid FEN"));
	CHECK(req.moves.empty());
    }
}

static void test_same_start(){
    PositionRequest a=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
    PositionRequest b=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/4K3 b - - 0 1"));
    PositionRequest c=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/3K4 w - - 0 1"));
    CHECK(SameStart(a.start,a.start));
    CHECK(!SameStart(a.start,b.start));
    CHECK(!SameStart(a.start,c.start));
    CHECK(SameStart(StandardStart(),StandardStart()));
    CHECK(!SameStart(StandardStart(),a.start));
    PositionRequest d=ParsePosition(split("position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    PositionRequest e=ParsePosition(split("position fen r3k2r/8/8/8/8/8/8/R3K2R w Kkq - 0 1"));
    CHECK(SameStart(d.start,d.start));
    CHECK(!SameStart(d.start,e.start));
}

static void test_setup_keys(){
    // Without castling rights: from an empty board. The cursor starts on
    // a2 (a7 when setting up Black); the kings last.
    StartPosition st=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/3QK3 w - - 0 1")).start;
    CHECK_EQ(SetupKeys(st,true),std::string("eRRRDqcRUUUUUUUkcDDDDDDDk"));
    CHECK_EQ(SetupKeys(st,false),std::string("ecRRRDDDDDDqRkcUUUUUUUkc"));
    st.white=false;
    CHECK_EQ(SetupKeys(st,true),std::string("eRRRDqcRUUUUUUUkcDDDDDDDkc"));
    // With castling rights: from the start position, emptying the squares
    // that change ('0'). The rooks that lose their rights are typed again.
    st=ParsePosition(split("position fen r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 1")).start;
    std::string keys="RD0R0R0RR0R0"        // b1 c1 d1 f1 g1
	"LLLLLLU0R0R0R0R0R0R0R0"            // a2..h2
	"LLLLLLLUUUUU0R0R0R0R0R0R0R0"       // a7..h7
	"LLLLLLU0R0R0RR0R0"                 // b8 c8 d8 f8 g8
	"LLLLLLDDDDDDDr"                    // the rook on a1 again
	"c"
	"RRRRRRRUUUUUUUr"                   // the rook on h8 again
	"c";
    CHECK_EQ(SetupKeys(st,true),keys);
    // Only what differs from the start position
    st=ParsePosition(split("position fen rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1")).start;
    CHECK(!st.standard);
    CHECK_EQ(SetupKeys(st,true),std::string("RRRR0UUUUU0DDDpcUpc"));
}

static void test_board_after(){
    std::vector<std::string> game=split("e2e4 e7e5 g1f3 b8c6");
    CHECK_EQ(CommonMoves(game,split("e2e4 e7e5 f1c4")),(size_t)2);
    CHECK_EQ(CommonMoves(game,game),(size_t)4);
    Board b,c;
    BoardAfter(StandardStart(),game,0,b);
    CHECK(IsStartPosition(b));
    BoardAfter(StandardStart(),game,3,b);
    make_board(c,
	       "rnbqkbnr"
	       "pppp.ppp"
	       "........"
	       "....p..."
	       "....P..."
	       ".....N.."
	       "PPPP.PPP"
	       "RNBQKB.R");
    CHECK(memcmp(b,c,sizeof(b))==0);
    StartPosition st=ParsePosition(split("position fen 4k3/8/8/8/8/8/8/R3K3 w Q - 0 1")).start;
    BoardAfter(st,split("e1c1"),1,b);
    CHECK_EQ(b[2][0],'K');
    CHECK_EQ(b[3][0],'R');
}

static void test_plan_sync(){
    std::vector<std::string> game=split("e2e4 e7e5 g1f3");
    CHECK_EQ(PlanSync(game,split("e2e4 e7e5 g1f3")),SYNC_DONE);
    CHECK_EQ(PlanSync(split(""),split("")),SYNC_DONE);
    // extend the game
    CHECK_EQ(PlanSync(game,split("e2e4 e7e5 g1f3 b8c6")),SYNC_SEND_MOVE);
    CHECK_EQ(PlanSync(split(""),split("d2d4")),SYNC_SEND_MOVE);
    // the target is shorter or differs late: take back
    CHECK_EQ(PlanSync(game,split("e2e4 e7e5")),SYNC_TAKEBACK);
    CHECK_EQ(PlanSync(game,split("e2e4 e7e5 b1c3")),SYNC_TAKEBACK);
    CHECK_EQ(PlanSync(game,split("")),SYNC_TAKEBACK);
    CHECK_EQ(PlanSync(game,split("d2d4")),SYNC_TAKEBACK);
    // up to four takebacks, then a new game is quicker
    std::vector<std::string> long_game=
	split("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6");
    CHECK_EQ(PlanSync(long_game,split("e2e4 e7e5")),SYNC_TAKEBACK);
    CHECK_EQ(PlanSync(long_game,split("e2e4")),SYNC_NEW_GAME);
    CHECK_EQ(PlanSync(long_game,split("d2d4 d7d5")),SYNC_NEW_GAME);
}

// Parses an analysis row, "" if it is not one; else "cp|mate N pv...",
// with "?" at the end if the line was not completely understood.
static std::string analysis(const char *row, bool white){
    Analysis a;
    if(!ParseAnalysis(row,white,a)){
	return "";
    }
    char buf[32];
    snprintf(buf,sizeof(buf),"%s %d",a.mate?"mate":"cp",a.score);
    std::string r=buf;
    for(size_t i=0;i<a.pv.size();i++){
	r+=" "+a.pv[i];
    }
    return a.complete?r:r+" ?";
}

static void test_parse_analysis(){
    // as shown by Psion (rows are padded to 80 columns)
    CHECK_EQ(analysis("-00.4 E2-E4 D7-D5 B1-C3 G8-F6 C3xD5 F6xE4 D5-C3 D8-D4"
		      "                         ",true),
	     "cp -40 e2e4 d7d5 b1c3 g8f6 c3d5 f6e4 d5c3 d8d4");
    CHECK_EQ(analysis(" 00.1 E2-E4 D7-D5",true),"cp 10 e2e4 d7d5");
    CHECK_EQ(analysis("-00.0 E2-E4",true),"cp 0 e2e4");
    CHECK_EQ(analysis(" 23.0 E7xC5 G8-F7 C7-C8",true),"cp 2300 e7c5 g8f7 c7c8");
    // castling is shown as a king move; other notations are accepted too
    CHECK_EQ(analysis(" 00.4 G6-H5 C8-D7 E1-G1",false),
	     "cp 40 g6h5 c8d7 e1g1");
    CHECK_EQ(analysis(" 00.4 O-O E8-G8 O-O-O",true),
	     "cp 40 e1g1 e8g8 e1c1");
    CHECK_EQ(analysis(" 00.4 A7-A8Q B2xA1=N C7-C8(R)",true),
	     "cp 40 a7a8q b2a1n c7c8r");
    CHECK_EQ(analysis(" 00.4 E2-E4+ D7-D5#",true),"cp 40 e2e4 d7d5");
    // mates: the length of the line gives the number of moves
    CHECK_EQ(analysis(" 99.9 C4xF7 E8-E7 C3-D5",true),"mate 2 c4f7 e8e7 c3d5");
    CHECK_EQ(analysis(" 99.9 H5xF7",true),"mate 1 h5f7");
    CHECK_EQ(analysis("-99.9 E8-E7 C3-D5",false),"mate -1 e8e7 c3d5");
    CHECK_EQ(analysis("-99.9 G7-G6 H5xF7 E8-E7 F7-F6",false),
	     "mate -2 g7g6 h5f7 e8e7 f7f6");
    // a row Psion was redrawing: blanks in place of changed characters
    CHECK_EQ(analysis(" 12.  C6-C7 D4- 5 C7-C8",true),"");
    CHECK_EQ(analysis(" 12.4 C6-C7 D4- 5 C7-C8",true),"cp 1240 c6c7 ?");
    // not analysis
    CHECK_EQ(analysis("",true),"");
    CHECK_EQ(analysis("                    ",true),"");
    CHECK_EQ(analysis("PROMOTE TO Q,R,B,N ?",true),"");
    CHECK_EQ(analysis("ILLEGAL MOVE",true),"");
    CHECK_EQ(analysis(" 00.4",true),"");
    CHECK_EQ(analysis(" 00.4 HELLO",true),"");
    CHECK_EQ(analysis("1.5 E2-E4",true),"cp 150 e2e4");
    CHECK_EQ(analysis("-.5 E2-E4",true),"");
}

// Checks a line from a diagram; the line as completed, "" if rejected.
static std::string check_pv(const char *diagram, bool white, const char *line){
    Board b;
    make_board(b,diagram);
    std::vector<std::string> pv=split(line);
    if(!CheckPv(b,white,pv)){
	return "";
    }
    std::string r;
    for(size_t i=0;i<pv.size();i++){
	r+=(i?" ":"")+pv[i];
    }
    return r;
}

static void test_check_pv(){
    const char *start=
	"rnbqkbnr" "pppppppp" "........" "........"
	"........" "........" "PPPPPPPP" "RNBQKBNR";
    CHECK_EQ(check_pv(start,true,"e2e4 e7e5 g1f3"),"e2e4 e7e5 g1f3");
    // the wrong side, an empty square, a capture of an own piece
    CHECK_EQ(check_pv(start,false,"e2e4"),"");
    CHECK_EQ(check_pv(start,true,"e2e4 e2e3"),"");
    CHECK_EQ(check_pv(start,true,"e3e4"),"");
    CHECK_EQ(check_pv(start,true,"d1d2"),"");
    // a move missing from the line: the sides no longer alternate
    CHECK_EQ(check_pv(start,true,"e2e4 g1f3"),"");
    // castling moves the rook, which can then move
    const char *castle=
	"r...k..r" "pppppppp" "........" "........"
	"........" "........" "PPPPPPPP" "R...K..R";
    CHECK_EQ(check_pv(castle,true,"e1g1 e8c8 f1f2"),"");
    CHECK_EQ(check_pv(castle,true,"e1g1 e8c8 f1e1 d8e8"),
	     "e1g1 e8c8 f1e1 d8e8");
    // en passant removes the pawn taken
    const char *ep=
	"....k..." "...p...." "........" "....P..."
	"........" "........" "........" "....K...";
    CHECK_EQ(check_pv(ep,false,"d7d5 e5d6 e8d8 d5d4"),"");
    CHECK_EQ(check_pv(ep,false,"d7d5 e5d6 e8d8 d6d7"),"d7d5 e5d6 e8d8 d6d7");
    // promotions without a piece become queens; the queen can move on
    const char *promo=
	"........" "..P....." "........" ".......k"
	"........" "........" "......p." "K.......";
    CHECK_EQ(check_pv(promo,true,"c7c8 g2g1 c8c1 g1e3"),
	     "c7c8q g2g1q c8c1 g1e3");
    CHECK_EQ(check_pv(promo,true,"c7c8n h5g5 c8b6"),"c7c8n h5g5 c8b6");
    // ... or the piece with which the line works
    CHECK_EQ(check_pv(promo,true,"c7c8 h5g5 c8b6"),"c7c8n h5g5 c8b6");
    CHECK_EQ(check_pv(promo,true,"c7c8 h5g5 c8h8"),"c7c8q h5g5 c8h8");
    CHECK_EQ(check_pv(promo,true,"c7c8 h5g5 c8a5"),"");
    CHECK_EQ(check_pv(promo,true,"c7c8 h5g5 c8b7 g5f4"),"c7c8q h5g5 c8b7 g5f4");
    CHECK_EQ(check_pv(promo,true,"c7c8 h5g5 c8a6"),"c7c8q h5g5 c8a6");
    // from a game: Psion promoted to a knight
    const char *knight=
	"........" ".......P" "........" "........"
	"........" ".KQ....." "....p..." "...k....";
    CHECK_EQ(check_pv(knight,false,"e2e1 h7h8 e1f3 h8h1 d1e2"),
	     "e2e1n h7h8q e1f3 h8h1 d1e2");
    // pieces move their own way, sliding pieces not through others
    CHECK_EQ(check_pv(start,true,"g1g3"),"");
    CHECK_EQ(check_pv(start,true,"f1c4"),"");
    CHECK_EQ(check_pv(start,true,"e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7"),
	     "e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7");
    CHECK_EQ(check_pv(start,true,"e2e4 e7e5 e4e5"),"");
    CHECK_EQ(check_pv(start,true,"e2e4 e7e5 e4d5"),"");
    CHECK_EQ(check_pv(start,true,"a1a3"),"");
    CHECK_EQ(check_pv(start,true,"e2e4 e7e5 e1e3"),"");
    CHECK_EQ(check_pv(start,true,"b1d2"),"");
    CHECK_EQ(check_pv(start,true,"b1c3 b8c6 c3e4"),"b1c3 b8c6 c3e4");
    // castling through a piece
    CHECK_EQ(check_pv(start,true,"e1g1"),"");
    // no move may leave the own king in check
    CHECK_EQ(check_pv(start,true,"e2e4 f7f6 d1h5 e8f7"),"");
    CHECK_EQ(check_pv(start,true,"e2e4 f7f6 d1h5 g7g6"),
	     "e2e4 f7f6 d1h5 g7g6");
    CHECK_EQ(check_pv(start,true,"e2e4 f7f6 d1h5 a7a6"),"");
    // no castling out of or through check
    const char *castle_through_check=
	"....k..." "........" "........" "........"
	"........" ".......b" "........" "R...K..R";
    CHECK_EQ(check_pv(castle_through_check,true,"e1c1"),"e1c1");
    CHECK_EQ(check_pv(castle_through_check,true,"e1g1"),"");
    const char *castle_in_check=
	"....k..." "........" "........" "........"
	".b......" "........" "........" "R...K..R";
    CHECK_EQ(check_pv(castle_in_check,true,"e1c1"),"");
    CHECK_EQ(check_pv(castle_in_check,true,"e1g1"),"");
    CHECK_EQ(check_pv(castle_in_check,true,"e1f1"),"e1f1");
    // from a game: f8=Q would leave Kh6 in check, so it is f8=N+
    const char *mate=
	"........" ".b..QP.p" "p...p.k." ".p...p.."
	".....P.." ".Pp.P..." "P..r..PP" "....K...";
    CHECK_EQ(check_pv(mate,true,"f7f8 g6h6 e7g5"),"f7f8n g6h6 e7g5");
}

int main(){
    test_decode_square();
    test_learn_square_code();
    test_is_start_position();
    test_derive_simple_moves();
    test_derive_capture();
    test_derive_castling();
    test_derive_en_passant();
    test_derive_promotion();
    test_derive_rejects_impossible_changes();
    test_is_move_string();
    test_tokenize();
    test_parse_go();
    test_think_time();
    test_set_option();
    test_cycles_latency();
    test_elo_cycles();
    test_parse_position();
    test_parse_fen();
    test_same_start();
    test_setup_keys();
    test_board_after();
    test_plan_sync();
    test_parse_analysis();
    test_check_pv();
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
