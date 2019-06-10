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

#include "socket.h"
#include "gameplay.h"


#ifndef PORT
    #define PORT 52948
#endif
#define MAX_QUEUE 5


void add_player(struct client **top, int fd, struct in_addr addr);
void remove_player(struct client **top, int fd);
int find_network_newline(const char *buf, int n);
int Write(int fd, char *msg);
void remove_from_inactive(struct client **top, int fd);
int name_valid(struct client *active_head, char *new_user_input, int result);
int find_network_newline(const char *buf, int n);
int read_to_inbuf(struct client *p, int already_read);
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

/* Send messages in outbuf to all player in the active list.
*/
void broadcast(struct game_state *game, char *outbuf){
	struct client *player = game->head;
	while(player != NULL){
		// if(write(player->fd, outbuf, sizeof(outbuf)) == -1){
		// 	// TODO
		// 	// disconnect(game, player);
		// }
		Write(player->fd, outbuf);
		// printf("The bytes write for broadcast is: %d\n", bytes);
		player = player->next;
	}
}

void announce_state(struct game_state *game){
	//notify all players the current game state
	char game_state[MAX_MSG];
	status_message(game_state, game);
	broadcast(game, game_state);
}

/*	Announce all active player the current state, and the next turn 
*/
void announce_turn(struct game_state *game){
	struct client *top = game->head;
	//msg to other players
	char name[MAX_MSG];
	strcpy(name, "It's ");
	strcat(name, (game->has_next_turn)->name);
	strcat(name, "'s turn.\r\n");
	//msg to next player
	char msg[MAX_MSG];
	strcpy(msg, "Your guess?\r\n");
	if(Write((game->has_next_turn)->fd, msg)==-1){
		advance_turn(game);
		disconnect(game, game->has_next_turn);
	}
	else{
		while(top!=NULL){
			if(strcmp(top->name, (game->has_next_turn)->name)!=0){
				if(Write(top->fd, name)==-1){
					disconnect(game, top);
				}
			}
			top = top->next;
		}
	}
}

void announce_winner(struct game_state *game, struct client *winner){
	struct client *p = game->head;
	if(Write(winner->fd, "Game over! You win!\r\n")==-1){
		//TODO
		// disconnect(game, winner);
	}
	while(p != NULL){
		if(p->fd != winner->fd){
			char msg[MAX_MSG] = "Game over! ";
			strcat(msg, winner->name);
			strcat(msg, " won!\r\n");
			if(Write(p->fd, msg)==-1){
				//TODO
				// disconnect(game, p);
			}
		}
		p = p->next;
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

/* Write the whole msg into the fd.
 * Return -1 if disconnection. Otherwise, return 0.
*/
int Write(int fd, char *msg){
	//get the num of bytes before '\0', no matter how many "\r\n" in msg
	int size = strlen(msg);
	int i=0;
	int num_write;
	int left_to_write = strlen(msg);
	while(i<size){
		num_write = write(fd, msg+i, left_to_write);
		//if fd closed
		if(num_write==-1){
			return -1;
		}
		i += num_write;
		left_to_write -= num_write;
	}
	return 0;
}

/* Add a client to the head of the linked list
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

/* Remove a inactive player from inactive linekd list.
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
 * Also removes socket descriptor from allset 
 */
void remove_player(struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
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

/* If the name given is valid, add this user to active list, 
*/
int name_valid(struct client *active_head, char *new_user_input, int result) {
	//Case1, if name is too long
	if(result == -1){
		return 0;
	}
	else if(result==0){
		return 0;
	}
	//Case2, name already existed
	else{
		// new_user_input[where-2] = '\0';
		struct client *p;
	    for (p = active_head; p; p = p->next){
	    	if(strcmp(p->name, new_user_input)==0){
	    		return 0;
	    	}
	    }
	    return 1;
	}
}

int find_network_newline(const char *buf, int n) {
    for(int i=0;i<=n-2;i++){
        if(buf[i]=='\r' && buf[i+1]=='\n'){
            return i+2;
        }
    }
    return -1;
}

/* We keep read() until we get a whole line that includes a network newline OR it reaches the MAX_BUF
 * Return -1 if this output exceed MAX_BUF. Otherwise, return the index of '\0'.
 */
int read_to_inbuf(struct client *p, int already_read){
	//first set inptr
	p->in_ptr = p->inbuf + already_read;
	// printf("The already_read is: %d\n", already_read);
	int room = sizeof(p->inbuf) - already_read;
	int num_read = 0;
	num_read += already_read;
	// printf("Going into the while loop, now size of inbuf is: %lu\n", strlen(p->inbuf));
	int where;
	while((where=find_network_newline(p->inbuf, num_read)) == -1 && num_read < MAX_BUF){
		// num_read represents how man bytes read into new_user_input so far
		int add_on;
		if((add_on = read(p->fd, p->in_ptr, room)) == -1){
			perror("server: read from inactive player");
  			exit(1);
		}
		// printf("The value of where is: %d, value of add_on is: %d\n", where, add_on);
		// printf("The size of inbuf is: %lu, what is in the inbuf is: %s\n", strlen(p->inbuf), p->inbuf);
		// printf("I am read_to_inbuf, I am reading ....... fuck this shit\n");
		num_read += add_on;
		p->in_ptr += add_on;
		room -= add_on;
	}
	// printf("Done read, what i read is: %s, size of inbuf is: %lu\n", p->inbuf, strlen(p->inbuf));
	int result = find_network_newline(p->inbuf, MAX_BUF);
	//this means there is no network newline in it
	if(result == -1){
		return -1;
	}
	else{
		p->inbuf[result-2] = '\0';
		//return the index of '\0'
		return result-2;
	}
}

/* Remove disconnected active player, and anounce to all other players
 */
void disconnect(struct game_state *game, struct client *p) {
	char msg[MAX_MSG];
	strcpy(msg, "Goodbye ");
	strcat(msg, p->name);
	strcat(msg, "\r\n");
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
	strcpy(msg, (game->has_next_turn)->name);
	strcat(msg, " guesses: ");
	strcat(msg, letter);
	strcat(msg, "\r\n");

	if(num_appeared == 0){	//wrong guess
		game->guesses_left --;
		if(!check_over(game, dict_name)){
			//notify on server
			printf("%s is not in the word\n", letter);
			//notify only this players that he/she guesses wrong
			char not_in_msg[MAX_MSG];
			memset(not_in_msg, '\0', sizeof(not_in_msg));
			strncpy(not_in_msg, letter, 1);
			strcat(not_in_msg, " is not in the word\r\n");
			Write((game->has_next_turn)->fd, not_in_msg);
			broadcast(game, msg);
			// switch to another player
			advance_turn(game);
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

/* Check if game is over and reset the game if it's over.
*/
int check_over(struct game_state *game, char *dict_name) {
	if(game->guesses_left == 0){
		// printf("No guesses left. Game over.\n");
		printf("Evaluating for game_over\n");
		printf("Out of guesses, no one won.\n");
		broadcast(game, "No guesses left. Game over.\r\n");
		init_game(game, dict_name);
		printf("New game\n");
		printf("The new word is: %s\n", game->word);
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
		printf("The new word is: %s\n", game->word);
		broadcast(game, "\r\nLet's start a new game\r\n");
		announce_state(game);
		announce_turn(game);
		return 1;
	}
	//no player stays DOES NOT mean this game is over!!!
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
    
    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int)time(NULL));
    // Set up the file pointer outside of init_game because we want to 
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);
    
    printf("The word chosen is: %s\n", game.word);

    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;
    
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
            // char *greeting = WELCOME_MSG;
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
            if(FD_ISSET(cur_fd, &rset)) {  //目前在这个set里面的fd，我们需要检查他是有data，还是closed

                // Check if this socket descriptor is an active player
                
                for(p = game.head; p != NULL; p = p->next) {
                    if(cur_fd == p->fd) {
                        //TODO - handle input from an active client
                        
                        //Check if it's this player's turn:
                        //(1)if yes, read from its socket -> 
                        //if --> (read returns 0, (make this a wrapper_1 func)then it's dropped connection and anounce all players: "Goodbye xxx"; remove from active list using remove_player(), advance_turn())
                        //else --> (check if the input is valid);
                        	//NOTE: 有data不一定是完整的！！！
                        	//1)if valid, announce all players: "xxx guesses: x"; And update game status(init_game if necessary, also update head and has_next_turn) --> also announce all players(in the active list) current game states, announce_turn
                    		//2)if not valid(not a single lower case letter OR already guessed), 
                    		//tell this player:" Invalid input, give another input"; 
                    		//NOTICE: DON'T read(), we will read next time we come here
                        //(2)if no, 
                        	//read from its socket first, 
                    		//1)use the wrapper_1 to check if he disconnect:
                        	//2)if this player input anything(even a blank line): announce to THIS player: "It is not your turn to guess"; 
                    		//And print to stdout that: "xxx(this player) tries to guess out of turn!"

                    	//check if it's this player's turn
                    	if((game.has_next_turn)->fd == p->fd){	//if yes...
                    		// read into the inbuf of the client
                    		memset(p->inbuf, '\0', sizeof(p->inbuf));
	                    	int num_read = read(p->fd, p->inbuf, MAX_BUF);
	                    	//if this fd causes error or closed
	                    	if(num_read==-1){
	                    		perror("server: active player read");
	                    		exit(1);
	                    	}
	                    	else if(num_read==0){	//in this case, the fd has been closed
	                    		//so we are going to announce all players
	                    		//TODO, need to handle this case here
								disconnect(&game, p);
								//advance turn
								advance_turn(&game);
								announce_turn(&game);
	                    	}
	                    	//if this fd has data to read
	                    	else{
	                    		//first, check if input is valid.
	                    		//read all data from the fd
	                    		int result = read_to_inbuf(p, num_read);
	                    		// printf("I am reading from client: %s's window, this input is %s!!!!!!!!!!!!!!\n", p->name, p->inbuf);
	                    		//if input invalid
	                    		if(result == -1 || result != 1 || !(p->inbuf[0]>='a' && p->inbuf[0]<='z') || game.letters_guessed[(int) (p->inbuf[0]-'a')]){
	                    			char msg[MAX_MSG] = INVALID_INPUT;
	                    			//socket closed
	                    			//TODO: consider Write() with disconnection
	                    			if(Write(p->fd, msg) == -1){
	                    				advance_turn(&game);
	                    				// this will idsconnect and announce to all other players
	                    				disconnect(&game, p);	
	                    			}
	                    		}
	                    		//valid input
	                    		else{
	                    			// this function will advance turn if needed
	                    			make_move(p->inbuf[0], &game, argv[1]);
	                    			//see if game over, and reset game as needed
	                    		}
	                    	}
                    	}
                    	//not this player's turn
                    	else{
                    		char buf[MAX_BUF];
                    		int num_read = read(p->fd, buf, MAX_BUF);
                    		if(num_read==-1){
                    			perror("server: inactive player input");
                    			exit(1);
                    		}
                    		else if(num_read==0){
                    			disconnect(&game, p);
                    		}
                    		// if this player input anything
                    		else{
                    			if(Write(p->fd, "It is not your turn to guess\r\n")==-1){
                    				disconnect(&game, p);
                    			}
                    		}

                    	}

                        break;
                    }
                }
        
                // Check if any new players are entering their names
                for(p = new_players; p != NULL; p = p->next) {

                    if(cur_fd == p->fd) {
                        // TODO - handle input from an new client who has
                        // not entered an acceptable name.
                        // call read() check if it's closed or has data
                        //if closed --> remove_player() from inactive linked list
                        //if has data --> 
                        	//if valid name(not existing name & not empty string & length not exceed MAX_NAME) --> set struct client.name; remove_player() from inactive list and add_player() to active list
                    		//if not valid --> ask player for another name
                    		//NOTICE: ALSO DON'T read() again
                    	memset(p->inbuf, '\0', sizeof(p->inbuf));
                    	int num_read = read(p->fd, p->inbuf, MAX_NAME);
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
                        	int result = read_to_inbuf(p, num_read);
	                    	// Start checking if name is valid
	                    	if(name_valid(game.head, p->inbuf, result) == 0){
	                    		char msg[MAX_MSG];
	                    		strcpy(msg, INVALID_NAME);
	                    		strcat(msg, "\r\n");
	                    		//We do not need to check if this fd is closed here, 
	                    		//because next time when we read() here, our code above will deal with that.
	                    		if(write(p->fd, msg, sizeof(msg)) == -1){
	                    			perror("server: write to inactive player");
	                    			exit(1);
	                    		}
	                    	}
	                    	//the name given is valid
	                    	else{
	                    		add_player(&(game.head), p->fd, p->ipaddr);
	                    		//give name to the newli-added player
	                    		strncpy((game.head)->name, p->inbuf, strlen(p->inbuf) + 1);

	                    		remove_from_inactive(&new_players, p->fd);
	                    		//announce all other players that this player joined the game, and print to server
	                    		char msg[MAX_MSG];
	                    		strncpy(msg, (game.head)->name, strlen((game.head)->name)+1);
	                    		strcat(msg, " has just joined. \r\n");
	                    		printf("%s joined the game!\n", (game.head)->name);
	                    		broadcast(&game, msg);
	                    		// show current status of the game to this player individually
	                    		char status[MAX_MSG];
	                    		status_message(status, &game);
	                    		Write(cur_fd, status);
	                    		//Note: all new commers come from here, 
	                    		//so we need to add them to active list(done above) and also put them in the turn list(do below)

	                    		//if this is our first player
	                    		if(game.has_next_turn == NULL){
	                    			game.has_next_turn = game.head;
	                    		}
	                    		//if it's not the first player, we don't need to do anything. Because the advance order follow game.head list

	                    		//announce turn ONLY to this player
	                    		char name[MAX_MSG];
	                    		if(strcmp((game.has_next_turn)->name, (game.head)->name)==0){
	                    			strcpy(name, "Your guess?\r\n");
	                    		}
	                    		else{
	                    			strcpy(name, "It's ");
									strcat(name, (game.has_next_turn)->name);
									strcat(name, "'s turn.\r\n");
	                    		}
								Write((game.head)->fd, name);
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


