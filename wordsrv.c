#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "socket.h"
#include "gameplay.h"


#ifndef PORT
    #define PORT 52949
#endif
#define MAX_QUEUE 5

void Write(int fd, char *msg, struct game_state *game, struct client *player);
void add_player(struct client **top, int fd, struct in_addr addr);
void remove_player(struct client **top, int fd);
int find_network_newline(const char *buf, int n);
void remove_from_inactive(struct client **top, int fd);
int name_valid(struct client *active_head, char *new_user_input, int result);
void disconnect(struct game_state *game, struct client *p);
void make_move(char input, struct game_state *game, char *dict_name);
int check_over(struct game_state *game, char *dict_name);

/* These are some of the function prototypes that we used in our solution 
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */
/* Send the message in outbuf to all clients */
void broadcast(struct game_state *game, char *outbuf);
void announce_turn(struct game_state *game);
void announce_winner(struct game_state *game, struct client *winner);
/* Move the has_next_turn pointer to the next active client */
void advance_turn(struct game_state *game);


/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;

/* A wrapper function for write().
*/
void Write(int fd, char *msg, struct game_state *game, struct client *player){
	if(write(fd, msg, strlen(msg))==-1){
		//if it's this player's turn
		
		if (game->has_next_turn->fd==fd){
			advance_turn(game);
			if(game->has_next_turn == player){
				game->has_next_turn = NULL;
			}
			disconnect(game, player);
		}
		//if not this player's turn
		else {
			disconnect(game, player);
			if (game->head == NULL){
				game->has_next_turn = NULL;
			}
		}
	}
}

/* Send messages in outbuf to all player in the active list.
*/
void broadcast(struct game_state *game, char *outbuf){
	struct client *player = game->head;
	while(player != NULL){
		Write(player->fd, outbuf, game, player);
		player = player->next;
	}
}

/* Announce the current state of the game.
*/
void announce_state(struct game_state *game){
	//notify all players the current game state
	char game_state[MAX_MSG];
	status_message(game_state, game);
	broadcast(game, game_state);
}

/*	Announce all active player the current state, and the next turn.
*/
void announce_turn(struct game_state *game){
	struct client *top = game->head;
	if (top!=NULL){
		//msg to other players
		char name[MAX_MSG];
		sprintf(name, "It's %s's turn.\r\n", (game->has_next_turn)->name);
		//msg to next player
		char msg[MAX_MSG];
		sprintf(msg, "Your guess?\r\n");
		Write((game->has_next_turn)->fd, msg, game, top);
		
		while(top!=NULL){
			if(strcmp(top->name, (game->has_next_turn)->name)!=0){
				Write(top->fd, name, game, top);
			}
			top = top->next;
		}
	}
}

/* Announce to all user who is the winner.
*/
void announce_winner(struct game_state *game, struct client *winner){
	struct client *p = game->head;
	if (p!=NULL){
		char msg1[MAX_MSG];
		sprintf(msg1, "Game over! You win!\r\n");
		Write(winner->fd, msg1, game, p);
		while(p != NULL){
			if(p->fd != winner->fd){
				char msg[MAX_MSG];
				sprintf(msg, "Game over! %s won!\r\n", winner->name);
				Write(p->fd, msg, game, p);
			}
			p = p->next;
		}
	}
	
}

/* Advance to next player.
*/
void advance_turn(struct game_state *game){
	// game->has_next_turn = (game->has_next_turn)->next;
	if((game->has_next_turn)->next == NULL){
		game->has_next_turn = game->head;
	}
	else{
		game->has_next_turn = (game->has_next_turn)->next;
	}
}

/* Add a client to the head of the linked list.
 */
void add_player(struct client **top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Remove player in the inactive list, but will not close its fd.
*/
void remove_from_inactive(struct client **top, int fd){
	struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        free(*p);
        *p = t;
    }
    //we do nothing if there is no player in inactive list.
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset.
 */
void remove_player(struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
    	if(strlen((*p)->name)!=0){
    		printf("%s disconnected\n", (*p)->name);
    	}
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                 fd);
    }
}

/* If the name given is valid, add this user to active list, result is position of '\0'
*/
int name_valid(struct client *active_head, char *new_user_input, int result) {
	//Case1, name is empty
	if(result==0){
		return 0;
	}
	//Case2, name already existed
	else{
		struct client *p;
	    for (p = active_head; p; p = p->next){
	    	if(strcmp(p->name, new_user_input)==0){
	    		return 0;
	    	}
	    }
	    return 1;
	}
}

/* Return 1 plus index of '\n'. If not found any network newline, return -1.
*/
int find_network_newline(const char *buf, int n) {
    for(int i=0;i<=n-2;i++){
        if(buf[i]=='\r' && buf[i+1]=='\n'){
            return i+2;
        }
    }
    return -1;
}

/* Remove disconnected active player, and anounce to all other players
 */
void disconnect(struct game_state *game, struct client *p) {
	char msg[MAX_MSG];
	sprintf(msg, "Goodbye %s\r\n", p->name);
	//remove player from active list
	remove_player(&(game->head), p->fd);
	broadcast(game, msg);
}

/* Make a move. And update game status.
 * Also announce if this guess is correct or not.
 */
void make_move(char input, struct game_state *game, char *dict_name) {
	char letter[2];
	strncpy(letter, &input, 1);
	letter[1]='\0';

	int num_appeared = 0;
	for(int i=0; i <strlen(game->word); i++){
		if(game->word[i] == input){
			game->guess[i] = input;
			num_appeared ++;
		}
	}
	game->letters_guessed[(int) (input-'a')] = 1;
	char msg[MAX_MSG];
	sprintf(msg, "%s guesses: %s\r\n", (game->has_next_turn)->name, letter);
	printf("%s guesses: %s\n", (game->has_next_turn)->name, letter);

	if(num_appeared == 0){	//wrong guess
		game->guesses_left --;
		if(!check_over(game, dict_name)){
			//notify on server
			printf("%s is not in the word\n", letter);
			//notify only this players that he/she guesses wrong
			char not_in_msg[MAX_MSG];
			memset(not_in_msg, '\0', sizeof(not_in_msg));
			sprintf(not_in_msg, "%s is not in the word\r\n", letter);

			struct client *current_player = game->has_next_turn;
			advance_turn(game);
			int result;
			result = write(current_player->fd, not_in_msg, strlen(not_in_msg));
			if (result==-1){
				//because we are writing to the current player
				disconnect(game, current_player);
				//if no player left
				if (game->head == NULL){
					game->has_next_turn = NULL;
				}
			}
			broadcast(game, msg);
			announce_state(game);
			announce_turn(game);
		}
	}
	else{
		if(!check_over(game, dict_name)){
			broadcast(game, msg);
			announce_state(game);
			announce_turn(game);
		}
	}
}

/* Check if game is over and reset if it's over. Also announce info to all players.
*/
int check_over(struct game_state *game, char *dict_name) {
	if(game->guesses_left == 0){
		printf("Evaluating for game_over\n");
		printf("Out of guesses, no one won.\n");
		broadcast(game, "No guesses left. Game over.\r\n");
		init_game(game, dict_name);
		printf("New game\n");
		broadcast(game, "\r\nLet's start a new game\r\n");
		advance_turn(game);
		announce_state(game);
		announce_turn(game);
		return 1;
	}
	else if(strcmp(game->word, game->guess)==0){
		char answer[MAX_MSG];
		memset(answer, '\0', sizeof(answer));
		strcpy(answer, "The word was ");
		strcat(answer, game->word);
		strcat(answer, ".\r\n");
		broadcast(game, answer);

		announce_winner(game, game->has_next_turn);
		printf("Game over. %s won!\n", (game->has_next_turn)->name);
		init_game(game, dict_name);
		broadcast(game, "\r\nLet's start a new game\r\n");
		announce_state(game);
		announce_turn(game);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv) {
    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;
    
    if(argc != 2){
        fprintf(stderr,"Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }
    
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if(sigaction(SIGPIPE, &sa, NULL) == -1) {
	    perror("sigaction");
	    exit(1);
	}

    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int)time(NULL));
    // Set up the file pointer outside of init_game because we want to 
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);
	// printf("finished init game....\n");
    init_game(&game, argv[1]);
    
    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;
    printf("finished init gamesss....\n");
    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;
    
    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);
    
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            continue;
        }

        //The select function blocks the program until input or output is ready on a specified set of file descriptors, 
        //or until a timer expires, whichever comes first. 
        if (FD_ISSET(listenfd, &rset)){
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_player(&new_players, clientfd, q.sin_addr);
            char *greeting = "Welcome to our word game. What is your name? \r\n";
            if(write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                remove_player(&(new_players), clientfd);
            };
        }
        
        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be 
         * valid.
         */
        int cur_fd;
        for(cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if(FD_ISSET(cur_fd, &rset)) {
                // Check if this socket descriptor is an active player
                for(p = game.head; p != NULL; p = p->next) {
                    if(cur_fd == p->fd) {
                        //TODO - handle input from an active client
                    	//check if it's this player's turn
                    	if((game.has_next_turn)->fd == p->fd){	//if yes...
                    		// read into the inbuf of the client
	                    	int num_read = read(p->fd, p->in_ptr, MAX_BUF);
	                    	printf("Read %d bytes\n", num_read);
	                    	//if this fd causes error or closed
	                    	if(num_read==-1){
	                    		perror("server: active player read");
	                    		exit(1);
	                    	}
	                    	else if(num_read==0){
	                    		//if this is the last one to remove
	                    		if(strcmp(game.head->name, p->name)==0 && game.head->next == NULL){
	                    			game.has_next_turn = NULL;
	                    		}
	                    		else{
	                    			advance_turn(&game);
	                    			announce_turn(&game);
	                    		}
								disconnect(&game, p);
	                    	}
	                    	//if this fd has data to read
	                    	else{
	                    		//check if there is a "\r\n" has been read
	                        	int where;
	                        	//if find a network newline in inbuf
	                        	if((where = find_network_newline(p->inbuf, MAX_BUF))!=-1){
	                        		p->inbuf[where-2] = '\0';
	                        		//if input invalid
		                    		if(where-2 == 0 || where-2 != 1 || !(p->inbuf[0]>='a' && p->inbuf[0]<='z') || game.letters_guessed[(int) (p->inbuf[0]-'a')]){
		                    			char msg[MAX_MSG] = INVALID_INPUT;
		                    			//socket closed
		                    			Write(p->fd, msg, &game, p);
		                    		}
		                    		//valid input
		                    		else{
		                    			// this function will advance turn if needed
		                    			//see if game over, and reset game as needed
		                    			make_move(p->inbuf[0], &game, argv[1]);
		                    		}
		                    		// clear out inbuf and reset in_ptr
			                    	memset(p->inbuf, '\0', sizeof(p->inbuf));
			                    	p->in_ptr = p->inbuf;
	                        	}
	                        	//if did not find "\r\n"
	                        	else{
	                        		//update in_ptr
	                        		p->in_ptr += num_read;
	                        	}
	                    	}
                    	}
                    	//not this player's turn
                    	else{
                    		int num_read = read(p->fd, p->in_ptr, MAX_BUF);
                    		printf("Read %d bytes\n", num_read);
                    		if(num_read==-1){
                    			perror("server: inactive player input");
                    			exit(1);
                    		}
                    		else if(num_read==0){
                    			disconnect(&game, p);
                    		}
                    		// if this player input anything
                    		else{
                    			int where;
                    			//only write warning until the shole line has been read into the inbuf
                    			if((where = find_network_newline(p->inbuf, MAX_BUF))!=-1){
                    				char warning[MAX_MSG];
	                    			sprintf(warning, "It is not your turn to guess\r\n");
	                    			Write(p->fd, warning, &game, p);
	                    			// clear out inbuf and reset in_ptr
			                    	memset(p->inbuf, '\0', sizeof(p->inbuf));
			                    	p->in_ptr = p->inbuf;
                    			}
                    			else{
                    				p->in_ptr += num_read;
                    			}
                    			
                    		}
                    	}
                        break;
                    }
                }
        
                // Check if any inactive players are entering their names
                for(p = new_players; p != NULL; p = p->next) {

                    if(cur_fd == p->fd) {
                        // TODO - handle input from an new client who has
                    	int num_read = read(p->fd, p->in_ptr, MAX_NAME);
                    	printf("Read %d bytes\n", num_read);
                    	if(num_read == -1){
                        	perror("server: read from inactive player");
                        	exit(1);
                        }
                        //this mean this fd has been closed
                        else if(num_read==0){
                        	//so we need to delete this user from inactive list
                        	remove_player(&new_players, p->fd);
                			printf("An inactive player left\n");
                        }
                        else {
                        	//check if there is a "\r\n" has been read
                        	int where;
                        	//if find a network newline in inbuf
                        	if((where = find_network_newline(p->inbuf, MAX_BUF))!=-1){
                        		p->inbuf[where-2] = '\0';
                        		// if name is not valid
		                    	if(name_valid(game.head, p->inbuf, where-2) == 0){
		                    		memset(p->inbuf, '\0', sizeof(p->inbuf));
		                    		p->in_ptr = p->inbuf;
		                    		char msg[MAX_MSG];
		                    		sprintf(msg, "%s\r\n", INVALID_NAME);
		                    		if(write(p->fd, msg, strlen(msg)) == -1){
		                    			remove_player(&new_players, p->fd);
		                    		}
		                    	}
		                    	// if name is valid
		                    	else{
		                    		add_player(&(game.head), p->fd, p->ipaddr);
		                    		memset((game.head)->inbuf, '\0', sizeof((game.head)->inbuf));
		                    		memset((game.head)->name, '\0', sizeof((game.head)->name));
		                    		//if this is our first player
		                    		if(game.has_next_turn == NULL){
		                    			game.has_next_turn = game.head;
		                    		}
		                    		//give name to the newli-added player
		                    		strncpy((game.head)->name, p->inbuf, strlen(p->inbuf) + 1);

		                    		remove_from_inactive(&new_players, p->fd);
		                    		//announce all other players that this player joined the game, and print to server
		                    		char msg[MAX_MSG];
		                    		sprintf(msg, "%s has just joined.\r\n", (game.head)->name);
		                    		printf("%s joined the game!\n", (game.head)->name);
		                    		broadcast(&game, msg);
		                    		// show current status of the game to this player individually
		                    		char status[MAX_MSG];
		                    		status_message(status, &game);
		                    		
		                    		//check if this player has been removed while broadcast above
		                    		if(FD_ISSET(cur_fd, &allset)){
		                    			Write(cur_fd, status, &game, game.head);
		                    			announce_turn(&game);
		                    		}
		                    	}
                        	}
                        	else{
                        		//update in_ptr
                        		p->in_ptr = p->in_ptr + num_read;
                        	}
                    	}
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

