#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "chess.h"

namespace psion {

    // More plies than this to take back and a new game is quicker.
    const size_t MAX_TAKEBACKS=4;

    static char learned_codes[256];

    void LearnSquareCode(unsigned char v, char piece){
	learned_codes[v]=piece;
    }

    // Square codes: bit 0x80 is set for Black. Bit 0x40 (and 0x04 for
    // rooks and kings) is some flag that does not change the piece.
    char DecodeSquare(unsigned char v){
	char c;
	if(v==0){
	    return '.';
	}
	switch(v&0x3F){
	case 0x2C: c='P'; break;
	case 0x24: c='N'; break;
	case 0x1C: c='B'; break;
	case 0x0A: case 0x0E: c='R'; break;
	case 0x17: c='Q'; break;
	case 0x01: case 0x05: c='K'; break;
	default: return learned_codes[v]?learned_codes[v]:'?';
	}
	return (v&0x80)?c-'A'+'a':c;
    }

    bool IsStartPosition(const Board board){
	const char *rank1="RNBQKBNR";
	const char *rank8="rnbqkbnr";
	int f,r;
	for(f=0;f<8;f++){
	    if(board[f][0]!=rank1[f] || board[f][1]!='P' ||
	       board[f][6]!='p' || board[f][7]!=rank8[f]){
		return false;
	    }
	    for(r=2;r<6;r++){
		if(board[f][r]!='.'){
		    return false;
		}
	    }
	}
	return true;
    }

    static bool own_piece(char c, bool white){
	return c!='.' && (isupper(c)!=0)==white;
    }

    bool DeriveMove(const Board before, const Board after, bool white,
		    std::string &move){
	int from[2],to[2],nfrom=0,nto=0,other=0,f,r,i;
	int ffrom,rfrom,fto,rto;
	for(f=0;f<8;f++){
	    for(r=0;r<8;r++){
		char b=before[f][r],a=after[f][r];
		if(a==b){
		    continue;
		}
		if(own_piece(b,white)){
		    if(nfrom==2){
			return false;
		    }
		    from[nfrom++]=8*f+r;
		}
		if(own_piece(a,white)){
		    if(nto==2){
			return false;
		    }
		    to[nto++]=8*f+r;
		}
		if(own_piece(a,!white)){
		    return false;  // opponent piece appeared or changed
		}
		if(own_piece(b,!white) && a=='.'){
		    other++;       // captured en passant
		}
	    }
	}
	if(nfrom==2 && nto==2){       // castling: report the king move
	    char king=white?'K':'k';
	    bool king_from=false,king_to=false;
	    for(i=0;i<2;i++){
		if(before[from[i]/8][from[i]%8]==king){
		    from[0]=from[i];
		    king_from=true;
		}
		if(after[to[i]/8][to[i]%8]==king){
		    to[0]=to[i];
		    king_to=true;
		}
	    }
	    if(!king_from || !king_to){
		return false;
	    }
	}else if(nfrom!=1 || nto!=1){
	    return false;
	}
	ffrom=from[0]/8; rfrom=from[0]%8; fto=to[0]/8; rto=to[0]%8;
	char piece=before[ffrom][rfrom];
	if(other>1 || (other==1 && toupper(piece)!='P')){
	    return false;
	}
	move="";
	move+=(char)('a'+ffrom); move+=(char)('1'+rfrom);
	move+=(char)('a'+fto); move+=(char)('1'+rto);
	if(toupper(piece)=='P' && toupper(after[fto][rto])!='P'){
	    move+=(char)tolower(after[fto][rto]);
	}
	return true;
    }

    bool IsMoveString(const std::string &m){
	if(m.size()!=4 && m.size()!=5){
	    return false;
	}
	if(m[0]<'a' || m[0]>'h' || m[1]<'1' || m[1]>'8' ||
	   m[2]<'a' || m[2]>'h' || m[3]<'1' || m[3]>'8'){
	    return false;
	}
	return m.size()==4 || (m[4]!=0 && strchr("qrbn",m[4])!=NULL);
    }

    std::vector<std::string> Tokenize(const char *line){
	std::vector<std::string> tokens;
	const char *c=line;
	while(*c){
	    while(*c && isspace(*c)){
		c++;
	    }
	    const char *b=c;
	    while(*c && !isspace(*c)){
		c++;
	    }
	    if(c>b){
		tokens.push_back(std::string(b,c-b));
	    }
	}
	return tokens;
    }

    GoParams ParseGo(const std::vector<std::string> &tokens){
	GoParams go;
	size_t i;
	go.wtime=go.btime=-1;
	go.winc=go.binc=go.movestogo=go.movetime=0;
	go.infinite=false;
	for(i=1;i<tokens.size();i++){
	    const std::string &t=tokens[i];
	    int v=i+1<tokens.size()?atoi(tokens[i+1].c_str()):0;
	    if(t=="wtime") go.wtime=v;
	    else if(t=="btime") go.btime=v;
	    else if(t=="winc") go.winc=v;
	    else if(t=="binc") go.binc=v;
	    else if(t=="movestogo") go.movestogo=v;
	    else if(t=="movetime") go.movetime=v;
	    else if(t=="infinite" || t=="ponder") go.infinite=true;
	}
	return go;
    }

    unsigned int ThinkTime(const GoParams &go, bool white,
			   unsigned int latency){
	int time=white?go.wtime:go.btime;
	int inc=white?go.winc:go.binc;
	int t;
	if(go.infinite){
	    return 0;
	}
	if(go.movetime>0){
	    t=go.movetime;
	}else if(time>=0){
	    t=time/(go.movestogo>0?go.movestogo:30)+3*std::max(inc,0)/4;
	    t=std::min(t,3*time/4);
	}else{
	    t=5000;  // depth, nodes, mate: not supported
	}
	return std::max(t-(int)latency,50);
    }

    PositionRequest ParsePosition(const std::vector<std::string> &tokens){
	static const char *start_fen=
	    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
	PositionRequest req;
	size_t i=2;
	req.supported=false;
	if(tokens.size()>1 && tokens[1]=="fen"){
	    std::string fen;
	    for(i=2;i<tokens.size() && tokens[i]!="moves";i++){
		fen+=(fen.empty()?"":" ")+tokens[i];
	    }
	    if(fen!=start_fen){
		req.error="FEN positions are not supported";
		return req;
	    }
	}else if(tokens.size()<2 || tokens[1]!="startpos"){
	    req.error="Invalid position command";
	    return req;
	}
	if(i<tokens.size() && tokens[i]=="moves"){
	    for(i++;i<tokens.size();i++){
		if(!IsMoveString(tokens[i])){
		    req.moves.clear();
		    req.error="Invalid move "+tokens[i];
		    return req;
		}
		req.moves.push_back(tokens[i]);
	    }
	}
	req.supported=true;
	return req;
    }

    SyncAction PlanSync(const std::vector<std::string> &game,
			const std::vector<std::string> &target){
	size_t p=0;
	if(game==target){
	    return SYNC_DONE;
	}
	while(p<game.size() && p<target.size() && game[p]==target[p]){
	    p++;
	}
	if(p==game.size()){
	    return SYNC_SEND_MOVE;
	}
	if(game.size()-p<=MAX_TAKEBACKS){
	    return SYNC_TAKEBACK;
	}
	return SYNC_NEW_GAME;
    }

}
