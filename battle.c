#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
 
#ifndef PORT
    #define PORT 23333
#endif

#define ENTERING 0
#define LOGIN 1
#define PENDING 2
#define MATCHED 3
#define IN_MATCH 4
#define SPEAKING 5

/** 
 * type client
 * fd - file descriptor for the client to write to and read from
 * ipaddr - ip address of the client
 * name - name of the character, filled with null bytes as client is added
 * status - indicate what the client is currently doing, including
 *          ENTERING - typing their name
 *          LOGIN - finished typing their names
 * 			PENDING - looking for an opponent
 * 			MATCHED - found an opponent
 * 			IN_MATCH - currently in combat and not talking
 * 			SPEAKING - typing what they want to speak during the match
 * hp - hitpoints the client has during a match
 * pm - number of powermoves left the client has during a match
 * strike - a number presenting the stage in match of the client
 * 			-1 - client not in match
 * 			0 - client in match, waiting for the opponent to strike
 * 			1 - client in match and in their turn, deciding their next move
 * inbuf - buffer used to handle user input
 * bufat - the next position in inbuf to read to
 * last_opponent - the last opponent the client encountered or themselves if they haven't fought
 * 				   or their last opponent has left (for convenience)
 * cur_opponent - the current opponent the client is in match with
 * next - pointer to the next client in the client list
 */
typedef struct clients {
    int fd;
    struct in_addr ipaddr;
    char name[20];
    int status;
    int hp;
    int pm;
    int strike;
    char inbuf[300];
    int bufat;
    struct clients *last_opponent;
    struct clients *cur_opponent;
    struct clients *next;
} client;

void addclient(int fd, struct in_addr addr);
void removeclient(int fd);

static void broadcast(client *except, char *s, int size);
int handleclient(client *p);
int check_quit(client *p);
int ask_name(client *p);
int welcome(client *p);
int find_opponent(client *p);
int move_to_bottom(client *p);
int start_match(client *p);

int find_network_newline(char *buf, int inbuf);
int bindandlisten(void);

int init_hp(void);
int init_pm(void);

int display1(client *p);
int display0(client *p);

int read_and_ignore(client *p);
int read_and_move(client *p);

int attack(client *p);
int powermove(client *p);
int speak(client *p);

int endgame_normal(client *p);
void endgame_quit(client *p);
 
client *head = NULL;
// Considering the need to modify the entire list frequently, declared
// head as a global variable.

int main(void) {
    int clientfd, maxfd, nready;
    client *p;
    socklen_t len;
    struct sockaddr_in q;
    struct timeval tv;
    fd_set allset;
    fd_set rset;
 	srand(time(NULL));
 	
    int i;
 
    int listenfd = bindandlisten();
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;
 
    while (1) {
		// Go over the client list and call find_opponent on each PENDING
        // client.
        for (p = head; p != NULL; p = p->next) {
			if (p->status == PENDING) {
				find_opponent(p);
			}
		}
		
		tv.tv_sec = 0;
        tv.tv_usec = 0; 
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
		
		if (nready == 0) {
            continue;
        }
		
		
        if (nready == -1) {
            perror("select");
            continue;
        }
 
        if (FD_ISSET(listenfd, &rset)){
            printf("a new client is connecting\n");
            len = sizeof(q);
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }
            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("connection from %s\n", inet_ntoa(q.sin_addr));
            addclient(clientfd, q.sin_addr);
            
        }
        

        for(i = 0; i <= maxfd; i++) {
			if (FD_ISSET(i, &rset)) {
				// If something is received from the client, handle the client.            
				for (p = head; p != NULL; p = p->next) {
					if (p->fd == i) {
                        int result = handleclient(p);
                        if (result == -1) {
							// If handleclient(p) returns -1, an error
							// Should have occured when reading or writing
							// to p or p has gone and the socket is closed
                            int tmp_fd = p->fd;
                            removeclient(p->fd);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
	}
    return 0;
}

int bindandlisten(void) {
    struct sockaddr_in r;
    int listenfd;
 
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }
    memset(&r, '\0', sizeof(r));// fill r with NULL bytes
    r.sin_family = AF_INET;//adddress inet
    r.sin_addr.s_addr = INADDR_ANY;//any IPv4 address
    r.sin_port = htons(PORT);//converts port from host byte order to network byte order.
 
 
    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
        perror("bind");
        exit(1);
    }
 
    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
    return listenfd;
}
 
int find_network_newline(char *buf, int inbuf) {
    int i;
    for (i=0;i<=inbuf;i++){
        if (buf[i] == '\n'){
			return i;
        }
    }
	return -1; // return the location of '\n' if found
}

void addclient(int fd, struct in_addr addr) {
	char buf[256];
    client *p = malloc(sizeof(client));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));
    p->fd = fd;
    p->ipaddr = addr;
    memset(&(p->name), '\0', 20); //fill name with null bytes
    memset(&(p->inbuf), '\0', 300); //fill inbuf with null bytes
    p->status = ENTERING;
    p->hp = 0;
    p->pm = 0;
    p->strike = -1;
    p->bufat = 0;
    p->last_opponent = p;
    p->cur_opponent = NULL;
    p->next = head;
    head = p;
    sprintf(buf, "What is your name? ");
    write(p->fd, buf, strlen(buf));
}

void removeclient(int fd) {
    client **p;
    char buf[256];
 
    for (p = &head; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        if ((*p)->status == IN_MATCH || (*p)->status == SPEAKING) {
			// If the player is in a match, end the match first
			endgame_quit(*p);
			for (p = &head; *p && (*p)->fd != fd; p = &(*p)->next);
			// Find the new location of p after rearranging the list when quitting
			// a game.
		}
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                 fd);
    }
    
	if (*p) {// Avoiding segfault
		client *t = (*p)->next;
		if ((*p)->status != ENTERING) {
			// If the client has already given their name, broadcast their leaving
			printf("%s is gone\n", (*p)->name);
			sprintf(buf, "**%s leaves**\n", (*p)->name);
			broadcast(*p, buf, strlen(buf));
		}
		free(*p);
		*p = t;
	} else {
		fprintf(stderr, "Trying to remove fd %d, but I missed it\n",
				 fd); 
	}
    
   
}

int handleclient(client *p) {
	// Send a client to the correct function.
	// If as signal is received when a client is not supposed to be
	// typing, check if the client has quit.
	int status = p->status;
	
	if (status == ENTERING) {
		return ask_name(p);
	}
	else if (status == LOGIN) {
		return check_quit(p);
	}
	else if (status == PENDING) {
		return check_quit(p);
	}
	else if (status == MATCHED) {
		return check_quit(p);
	}
	else if (status == IN_MATCH) {
		if (p->strike == 0) { //If it's not p's turn
			return read_and_ignore(p);
		}
		else { // If it is p's turn
			return read_and_move(p);
		}
	}
	else if (status == SPEAKING) {
		return speak(p);
	}
	else {
		return -1;
	}
}

int check_quit(client *p) {
	// As receiving a signal during the phase when the client should not
	// be typing, check if it is quitting, a.k.a. the socket is closed,
	// or an error occurs when typing.
	if (read(p->fd, &(p->inbuf)[p->bufat], 1) <= 0) {
		return -1;
	}
	return 0;
}
/** 
 * int ask_name(client *p)
 * Called by handleclient if client p types something when they are
 * ENTERING, a.k.a they are supposed to be typing their name.
 * Read the typed character immediately and store the entered message
 * in the client's inbuf.
 * When a new line is found before the client has entered anything, keep
 * prompting them to enter their name. Otherwise, consider as the client
 * has finished typing their name, and set their name as is, set their
 * status to LOGIN and send them to the welcome function.
 * @return return -1 if error occurs  or nothing is received when
 * 			reading from the client, so that -1 will be returned by
 * 			handleclient, and the client will be removed in main.
 * 			If the input exceeds 300 chars, kick the client out by
 * 			returning -1.
 * 			return 0 if no error occurs.
 */
int ask_name(client *p) {
    //If the input exceeds 300 chars, remove this client
    //If see a carriage return, stop and get the name from buffer.
	int pos;
	int len;
	len = read(p->fd, &(p->inbuf)[p->bufat], 1);
	if (len <= 0) {
		perror("read");
		return -1;
	}
	p->bufat = p->bufat + len;
	if ((pos = find_network_newline(p->inbuf, p->bufat)) != -1) {
		if (pos != 0) {
			if (pos < 19) {
				p->inbuf[pos] = '\0';
			}
			else {
				p->inbuf[19] = '\0';
			}
			strcpy(p->name, p->inbuf);
			memset(p->inbuf, '\0', 300);//Clear inbuf
			p->status = LOGIN;
			p->bufat = 0;
			printf("%s is connected\n", p->name);
			return welcome(p);
		}
	}
	else if (strlen(p->inbuf) >= 300) {
		return -1;
	}
	return 0;
}

/** 
 * int welcome(client *p)
 * Tell all the other clients when the client had logged in with their
 * name. Display a welcome message to p. Then set the status of p to
 * PENDING and send it to find an opponent
 * @return return -1 if error occurs when writing to the client,
 */
int welcome(client *p) {
	// Broadcast the entrance of new client
	char buf[256];
	sprintf(buf, "Welcome, %s! Awaiting opponent...\n", p->name);
	if (write(p->fd, buf, strlen(buf)) <= 0) {
		return -1;
	}
	sprintf(buf, "**%s enters the arena**\n", p->name);
	broadcast(p, buf, strlen(buf));
	move_to_bottom(p);// move the client to the end of the list
	p->status = PENDING;
	return find_opponent(p);
}

/**
 * int find_opponent(client *p)
 * Search through the current client list from top to bottom for once.
 * If a valid opponent is found, match them and send them to start_match.
 * Move them to the end of the client list so they have a lower priority
 * when new matches are being made after that.
 * Return 0 if a valid opponent is not found.
 */
int find_opponent(client *p) {
	// Look up for a valid opponent
	client *q;
	for (q = head; q; q = q->next) {
		if (q->status == PENDING && (q->last_opponent)->fd != p->fd
		    && q->fd != p->fd) {
			q->status = MATCHED;
			p->status = MATCHED;
			q->cur_opponent = p;
			p->cur_opponent = q;
			return start_match(p);
		}
	}
	return 0;
}


int move_to_bottom(client *p) {
    client *q;
	for (q = head; q->next != NULL; q = q->next);
	//q is the last in list
	if (p->fd != q->fd) { //if p is not the last in the list
		if (p->fd == head->fd) {
			client *r = p->next;
			head = r;
			p->next = NULL;
			q->next = p;
		}
		else {
			client *t;
			for (t = head; t->next != p && t->next != NULL; t = t->next);
			//t is before p in the list
			client *r = p->next;
			t->next = r;
			p->next = NULL;
			q->next = p;
		}
	}
    return 0;
}


int display1(client *p) { // display to the person in turn
	char buf[256];
	if (p->pm > 0) {
		// if there are powermoves left for p, display (p)ower as an option.
		sprintf(buf, "\nYour hitpoints: %d\nYour powermoves: %d\n\n%s's hitpoints: %d\n\n(a)ttack\n(p)owermove\n(s)peak something\n", p->hp, p->pm, (p->cur_opponent)->name, (p->cur_opponent)->hp);
	}
	else {
		sprintf(buf, "\nYour hitpoints: %d\nYour powermoves: %d\n\n%s's hitpoints: %d\n\n(a)ttack\n(s)peak something\n", p->hp, p->pm, (p->cur_opponent)->name, (p->cur_opponent)->hp);
	}
	return write(p->fd, buf, strlen(buf));
}


int display0(client *p) { // display to the person not yet in turn
	char buf[256];
	sprintf(buf, "\nYour hitpoints: %d\nYour powermoves: %d\n\n%s's hitpoints: %d\nWaiting for %s to strike...\n", p->hp, p->pm, (p->cur_opponent)->name, (p->cur_opponent)->hp, (p->cur_opponent)->name);
	return write(p->fd, buf, strlen(buf));
}


/**
 * int start_match(client *p)
 * Initialize stats for p and their opponent. Tell them who they are
 * encountering, decide who goes first and display message to them
 * accordingly.
 * return -1 if error occurs when writing to p
 */
int start_match(client *p) {
	char buf[256];
	//initialize stats
	(p->cur_opponent)->status = IN_MATCH;
	p->status = IN_MATCH;
	p->hp = init_hp();
	p->pm = init_pm();
	(p->cur_opponent)->hp = init_hp();
	(p->cur_opponent)->pm = init_pm();
	
	sprintf(buf, "You engage %s!\n", (p->cur_opponent)->name);
	if (write(p->fd, buf, strlen(buf)) <= 0) {
		perror("write");
		return -1;//remove client in the main() while loop
	}
	sprintf(buf, "You engage %s!\n", p->name);
	if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
		perror("write");
	}
	if (p->strike == -1) { //If the strike order has not been set for p
		p->strike = rand() % 2;
		if (p->strike == 1) {
			(p->cur_opponent)->strike = 0;
		}
		else {
			(p->cur_opponent)->strike = 1;
		}
	}
	if (p->strike == 1) {
		display0(p->cur_opponent);
		return display1(p);
	}
	else {
		display1(p->cur_opponent);
		return display0(p);
	}
}

int read_and_ignore(client *p) {
	// If the player is not in thier turn, read their input and ignore everything
	// unless it indicates that the socket is closed or error has occured.
	// Return -1 in those cases so the client is removed.
	if (read(p->fd, &(p->inbuf)[p->bufat], 1) <=0) {
		perror("read");
		return -1;
	}
	memset(p->inbuf, '\0', 300);
	return 0;
}

int read_and_move(client *p) {
	// If the player is in their turn read their input and process accordingly
	char buf[512];
	if (read(p->fd, &(p->inbuf)[p->bufat], 1) <=0) {
		perror("read");
		return -1;
	}
	if (p->inbuf[0] == 'a') {//attack
		memset(p->inbuf, '\0', 300);
		return attack(p);
	}
	else if (p->inbuf[0] == 'p') {//powermove
		if (p->pm > 0) { //If the player has powermoves left, allow p.
			memset(p->inbuf, '\0', 300);
			return powermove(p);
		}
		else {
			memset(p->inbuf, '\0', 300);
			return 0;
		}
	}
	else if (p->inbuf[0] == 's') {//speak
		sprintf(buf, "\nSpeak: ");
		if (write(p->fd, buf, strlen(buf)) <= 0) {
			perror("write");
			return -1;
		}
		p->status = SPEAKING;
		memset(p->inbuf, '\0', 300);//prepare an empty buf for speaking
	}//ignore all irrelevant input
	return 0;
}

int attack(client *p) {
	int damage;
	int min = 2;
	int max = 6;
	char buf[256];
	srand(time(NULL));
	damage = (rand()%(max-min+1))+min; // Generate the damege done
	(p->cur_opponent)->hp = (p->cur_opponent)->hp - damage;
	sprintf(buf, "\nYou hit %s for %d damage!", (p->cur_opponent)->name, damage);
	if (write(p->fd, buf, strlen(buf)) <= 0) {
		perror("write");
		return -1;
	}
	sprintf(buf, "\n%s hits you for %d damage!", p->name, damage);
	if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
		perror("write");
	}
	if ((p->cur_opponent)->hp <= 0) {
		// If the opponent loses all their hp, end the game and report p
		// as the winner
		return endgame_normal(p);
	}
	// If the game had not ended yet, make it the turn of the other player.
	p->strike = 0;
	(p->cur_opponent)->strike = 1;
	
	// Display messages to the two players accordingly.
	display1(p->cur_opponent);
	return display0(p); // Return -1 in display0 if error occurs when
						 // writing to p, so p is removed from client list
						 // in main().
}

int powermove(client *p) {
	int damage;
	int hit;
	int min = 2;
	int max = 6;
	char buf[256];
	srand(time(NULL));
	damage = 3 * ((rand()%(max-min+1))+min); // Generate the damage done, 3 times as normal attack
	hit = rand() % 2;// decides whether the attack hits(1) or not(0).
	if (hit) {
		(p->cur_opponent)->hp = (p->cur_opponent)->hp - damage;
		sprintf(buf, "\nYou hit %s for %d damage!", (p->cur_opponent)->name, damage);
		if (write(p->fd, buf, strlen(buf)) <= 0) {
			perror("write");
			return -1;
		}
		sprintf(buf, "\n%s hits you for %d damage!", p->name, damage);
		if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
			perror("write");
		}
		if ((p->cur_opponent)->hp <= 0) {
			return endgame_normal(p);
		}
	}
	else {
		// If the attack missed the target, report that.
		sprintf(buf, "\nYou missed!\n");
		if (write(p->fd, buf, strlen(buf)) <= 0) {
			perror("write");
			return -1;
		}
		sprintf(buf, "\n%s missed you!\n", p->name);
		if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
			perror("write");
		}
	}
	p->pm = p->pm - 1;// 1 powermove is consumed.
	p->strike = 0;
	(p->cur_opponent)->strike = 1;
	
	display1(p->cur_opponent);
	return display0(p);
}

/**
 * int speak(client *p)
 * If the client is SPEAKING, when they type something store their input
 * in their inbuf char by char. When they have hit enter, send the typed
 * message to their opponent.
 * If the input exceeds 300 chars, kick the client out. If the input
 * hits enter before entering anything, keep prompting for input.
 */
int speak(client *p) {
	char buf[256];
	int pos;
	int len;

	len = read(p->fd, &(p->inbuf)[p->bufat], 1);
	if (len <= 0) {
		perror("read");
		return -1; // Socket is closed or error occured. Client should
					// be removed.
	}
	p->bufat = p->bufat + len;
	if ((pos = find_network_newline(p->inbuf, p->bufat)) != -1) {
		if (pos != 0) {
			p->inbuf[pos] = '\0';
			sprintf(buf, "You speak: %s\n", p->inbuf);
			if (write(p->fd, buf, strlen(buf)) <= 0) {
				perror("write");
				return -1;
			}
			sprintf(buf, "%s takes a break to tell you:\n%s\n", p->name, p->inbuf);
			if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
				perror("write");
			}
			memset(p->inbuf, '\0', 300);
			p->bufat = 0;
			p->status = IN_MATCH;
			display0(p->cur_opponent);
			return display1(p);	
		}
	}
	else if (strlen(p->inbuf) >= 300) {
		return -1;
	}
	return 0;
}


int endgame_normal(client *p) {
	// When the game ends normally as p defeats their opponent,
	// set both of them PENDING and record them as the last_opponent
	// of each other.
	char buf[256];
	sprintf(buf, "\n%s gives up. You win!\n\nAwaiting next opponent...\n", (p->cur_opponent)->name);
	if (write(p->fd, buf, strlen(buf)) <= 0) {
		perror("write");
		return -1;
	}
	sprintf(buf, "\nYou are no match for %s. You scurry away...\n\nAwaiting next opponent...\n", p->name);
	if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
		perror("write");
	}
	p->strike = -1;
	(p->cur_opponent)->strike = -1;
	p->status = PENDING;
	(p->cur_opponent)->status = PENDING;
	p->last_opponent = p->cur_opponent;
	(p->cur_opponent)->last_opponent = p;
	move_to_bottom(p);
	move_to_bottom(p->cur_opponent); // Move both clients to the end of list
	return 0;
}


void endgame_quit(client *p) {
	// When the game ends when p leaves, p's opponent wins and is set
	// to be PENDING, while their "last_opponent" is set to the initial
	// value (themselves).
	char buf[256];
	sprintf(buf, "\n--%s dropped. You win!\n\nAwaiting next opponent...\n", p->name);
	if (write((p->cur_opponent)->fd, buf, strlen(buf)) <= 0) {
		perror("write");
	}
	(p->cur_opponent)->strike = -1;
	(p->cur_opponent)->status = PENDING;
	(p->cur_opponent)->last_opponent = (p->cur_opponent);
	// Clear "last opponent" history since last opponent is no longer in the arena
	move_to_bottom(p->cur_opponent);// Move p's opponent to the end of list
}


static void broadcast(client *except, char *s, int size) {
    client *p;
    for (p = head; p; p = p->next) {
		if (except != NULL) {//If one is indicated to be excluded from the broadcast
			if (except->fd != p->fd) {//Only broadcast to others
				if (write(p->fd, s, size) == -1) {
					perror("write");
				}
			}
		}
		else {
			if (write(p->fd, s, size) == -1) {
				perror("write");
			}
		}     
    }
}

int init_hp() { // Initialize hp
	int hp;
	int min = 20;
	int max = 30;
	hp = (rand()%(max-min+1))+min;
	return hp;
}

int init_pm() { // Initialize the number of powermoves
	int pm;
	int min = 1;
	int max = 3;
	pm = (rand()%(max-min+1))+min;
	return pm;
}
