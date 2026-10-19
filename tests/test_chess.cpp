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
    CHECK_EQ(go.wtime,-1);
    CHECK_EQ(go.btime,-1);
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
    go=ParseGo(split("go movetime 1500"));
    CHECK_EQ(go.movetime,1500);
    CHECK(ParseGo(split("go infinite")).infinite);
    CHECK(ParseGo(split("go ponder wtime 1000 btime 1000")).infinite);
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
    CHECK_EQ(ThinkTime(go,false,400),600u);
    // at most 3/4 of the remaining time
    go=ParseGo(split("go wtime 10000 btime 10000 movestogo 1"));
    CHECK_EQ(ThinkTime(go,true,400),7100u);
    // never less than 50 ms
    go=ParseGo(split("go wtime 100 btime 100"));
    CHECK_EQ(ThinkTime(go,true,400),50u);
    // no time information: a fixed default
    go=ParseGo(split("go depth 10"));
    CHECK_EQ(ThinkTime(go,true,400),4600u);
    go=ParseGo(split("go infinite"));
    CHECK_EQ(ThinkTime(go,true,400),0u);
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
    req=ParsePosition(split("position fen 8/8/8/4k3/8/8/4K3/8 w - - 0 1"));
    CHECK(!req.supported);
    CHECK_EQ(req.error,std::string("FEN positions are not supported"));
    req=ParsePosition(split("position startpos moves e2e4 junk"));
    CHECK(!req.supported);
    CHECK(req.moves.empty());
    CHECK_EQ(req.error,std::string("Invalid move junk"));
    req=ParsePosition(split("position"));
    CHECK(!req.supported);
    req=ParsePosition(split("position nonsense"));
    CHECK(!req.supported);
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
    test_parse_position();
    test_plan_sync();
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
