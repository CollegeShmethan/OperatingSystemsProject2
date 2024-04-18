/*
 ***************************************************************************
 * Clarkson University                                                     *
 * CS 444/544: Operating Systems, Spring 2024                              *
 * Project: Prototyping a Web Server/Browser                               *
 * Created by Daqing Hou, dhou@clarkson.edu                                *
 *            Xinchao Song, xisong@clarkson.edu                            *
 * April 10, 2022                                                          *
 * Copyright © 2022-2024 CS 444/544 Instructor Team. All rights reserved.  *
 * Unauthorized use is strictly prohibited.                                *
 ***************************************************************************
 */

#include "net_util.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

// File System
#include <fstream>

// Unordered Map
#include <unordered_map>

// Time (For Psuedo-Random Number Seeding)
#include <ctime>

// Testing
#include <iostream>

#define NUM_VARIABLES 26
#define NUM_SESSIONS 128
#define NUM_BROWSER 128
#define DATA_DIR "./sessions"
#define SESSION_PATH_LEN 128
// Storage file for sessions
#define SESSIONS_PATH "./sessions/session.dat"

typedef struct browser_struct {
    bool in_use;
    int socket_fd;
    int session_id;
} browser_t;

typedef struct session_struct {
    bool in_use;
    bool variables[NUM_VARIABLES];
    double values[NUM_VARIABLES];
} session_t;

static browser_t browser_list[NUM_BROWSER];                             // Stores the information of all browsers.
static std::unordered_map<int, session_t> session_list;			// Stores the information of all sessions.
static pthread_mutex_t browser_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the browser list.
static pthread_mutex_t session_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the session list.

// Returns the string format of the given session.
// There will be always 9 digits in the output string.
void session_to_str(int session_id, char result[]);

// Determines if the given string represents a number.
bool is_str_numeric(const char str[]);

// Process the given message and update the given session if it is valid.
bool process_message(int session_id, const char message[]);

// Broadcasts the given message to all browsers with the same session ID.
void broadcast(int session_id, const char message[]);

// Gets the path for the given session.
void get_session_file_path(int session_id, char path[]);

// Loads every session from the disk one by one if it exists.
void load_all_sessions();

// Saves the given sessions to the disk.
void save_session(int session_id);

// Assigns a browser ID to the new browser.
// Determines the correct session ID for the new browser
// through the interaction with it.
int register_browser(int browser_socket_fd);

// Handles the given browser by listening to it,
// processing the message received,
// broadcasting the update to all browsers with the same session ID,
// and backing up the session on the disk.
void * browser_handler(void * browser_socket_fd);

// Starts the server.
// Sets up the connection,
// keeps accepting new browsers,
// and creates handlers for them.
void start_server(int port);

/**
 * Returns the string format of the given session.
 * There will be always 9 digits in the output string.
 *
 * @param session_id the session ID
 * @param result an array to store the string format of the given session;
 *               any data already in the array will be erased
 */
void session_to_str(int session_id, char result[]) {
    memset(result, 0, BUFFER_LEN);
    session_t session = session_list[session_id];

    for (int i = 0; i < NUM_VARIABLES; i++) {
        if (session.variables[i]) {
            char line[32];

            if (session.values[i] < 1000) {
                sprintf(line, "%c = %.6f\n", 'a' + i, session.values[i]);
            } else {
                sprintf(line, "%c = %.8e\n", 'a' + i, session.values[i]);
            }

            strcat(result, line);
        }
    }
}

/**
 * Determines if the given string represents a number.
 *
 * @param str the string to determine if it represents a number
 * @return a boolean that determines if the given string represents a number
 */
bool is_str_numeric(const char str[]) {
    if (str == NULL) {
        return false;
    }

    if (!(isdigit(str[0]) || (str[0] == '-') || (str[0] == '.'))) {
        return false;
    }

    int i = 1;
    while (str[i] != '\0') {
        if (!(isdigit(str[i]) || str[i] == '.')) {
            return false;
        }
        i++;
    }

    return true;
}

/**
 * Process the given message and update the given session if it is valid.
 * If the message is valid, the function will return true; otherwise, it will return false.
 *
 * @param session_id the session ID
 * @param message the message to be processed
 * @return a boolean that determines if the given message is valid
 */
bool process_message(int session_id, const char message[]) {
	// Would prefer a full rewrite of this function to make only one acceptable path to return true, and otherwise instantly return false.
    char *token;
    int result_idx;
    double first_value;
    char symbol;
    double second_value;

    // Makes a copy of the string since strtok() will modify the string that it is processing.
    char data[BUFFER_LEN];
    strcpy(data, message);

    // Processes the result variable.
    token = strtok(data, " ");
	if (!token) {
		return false;
	}
    result_idx = token[0] - 'a';

	if (result_idx < 0 || result_idx > 25) {
		return false;
	}

    // Processes "=".
    token = strtok(NULL, " ");

	if (!token || token[0] != '=') {
		return false;
	}

    // Processes the first variable/value.
    token = strtok(NULL, " ");
	if (!token) {
		return false;
	}
    if (is_str_numeric(token)) {
        first_value = strtod(token, NULL);
    } else {
        int first_idx = token[0] - 'a';
	if (first_idx < 0 || first_idx > 25 || !session_list[session_id].variables[first_idx]) {
		return false;
	}
        first_value = session_list[session_id].values[first_idx];
    }

    // Processes the operation symbol.
    token = strtok(NULL, " ");
    if (token == NULL) {
        session_list[session_id].variables[result_idx] = true;
        session_list[session_id].values[result_idx] = first_value;
        return true;
    }
    symbol = token[0];

	if (symbol != '*' && symbol != '+' && symbol != '-' && symbol != '/') {
		return false;
	}

    // Processes the second variable/value.
    token = strtok(NULL, " ");
	if (!token) {
		return false;
	}
    if (is_str_numeric(token)) {
        second_value = strtod(token, NULL);
	if (second_value == '0' && symbol == '/') {
		return false;
	}
    } else {
        int second_idx = token[0] - 'a';
	if(second_idx < 0 || second_idx > 25 || !session_list[session_id].variables[second_idx]) {
		return false;
	}
        second_value = session_list[session_id].values[second_idx];
    }

    // No data should be left over thereafter.
    token = strtok(NULL, " ");
	if (token) {
		return false;
	}

    session_list[session_id].variables[result_idx] = true;

    if (symbol == '+') {
        session_list[session_id].values[result_idx] = first_value + second_value;
    } else if (symbol == '-') {
        session_list[session_id].values[result_idx] = first_value - second_value;
    } else if (symbol == '*') {
        session_list[session_id].values[result_idx] = first_value * second_value;
    } else if (symbol == '/') {
        session_list[session_id].values[result_idx] = first_value / second_value;
    }

    return true;
}

/**
 * Broadcasts the given message to all browsers with the same session ID.
 *
 * @param session_id the session ID
 * @param message the message to be broadcasted
 */
void broadcast(int session_id, const char message[]) {
    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (browser_list[i].in_use && browser_list[i].session_id == session_id) {
            send_message(browser_list[i].socket_fd, message);
        }
    }
}

/**
 * Gets the path for the given session.
 *
 * @param session_id the session ID
 * @param path the path to the session file associated with the given session ID
 */
void get_session_file_path(int session_id, char path[]) {
    sprintf(path, "%s/session%d.dat", DATA_DIR, session_id);
}

/**
 * Loads every session from the disk one by one if it exists.
 * Use get_session_file_path() to get the file path for each session.
 */
void load_all_sessions() {
	char path[SESSION_PATH_LEN];
	char c;
	int variable;
	std::ifstream session_file;
	std::ifstream sessions_list;
	session_t session;

	sessions_list.open(SESSIONS_PATH);

	if (!sessions_list.good()) {
		std::ofstream create_backup;
		create_backup.open(SESSIONS_PATH);
		create_backup.close();
	}

	int id = -1;
	int prev_id = -1;
	char c2;
	double val;
	while (sessions_list.good() && !sessions_list.eof()) {
		sessions_list >> id;
		sessions_list.get(c2);
		get_session_file_path(id, path);
		session_file.open(path);
		if (id == prev_id) {
			break;
		}
		if (!session_file.good()) {
			printf("Error acquiring Session %d information.\n", id);
			continue;
		}
		prev_id = id;
		while (!session_file.eof()) {
                        session_file.get(c);
			if (session_file.eof()) {
				break;
			}
                        variable = c - 'a';
                        if (variable < 0 || variable > 25) {
                                printf("Error with Session %d file formatting.\n", id);
				break;
                        }
                        for (int j = 0; j < 3; j++) {
                                session_file.get(c);
                        }
                        session_file >> val;
			if (val) {
				session_list[id].values[variable] = val;
				session_list[id].variables[variable] = true;
			} else {
				session_list[id].variables[variable] = false;
				session_list[id].values[variable] = 0.0;
                        }
			variable = -1;
			session_file.get(c);
                }
		session_file.close();
	}
}

/**
 * Saves the given sessions to the disk.
 * Use get_session_file_path() to get the file path for each session.
 *
 * @param session_id the session ID
 */
void save_session(int session_id) {
	char path[BUFFER_LEN];
	char result[BUFFER_LEN];
	bool saved = false;
	std::ofstream session_file;
	std::ifstream sessions_list;
	std::ofstream sessions_output;

	sessions_list.open(SESSIONS_PATH);

	int id = -1;
	char c;
	while (!sessions_list.eof()) {
		sessions_list >> id;
		sessions_list.get(c);
		if (id == -1) {
			break;
		}
		if (id == session_id) {
			saved = true;
		}
	}

	sessions_list.close();

	if (!saved) {
		sessions_output.open(SESSIONS_PATH);
		sessions_output << session_id;
		sessions_output << '\n';
		sessions_output.close();
	}

	get_session_file_path(session_id, path);
	session_file.open(path);

	session_to_str(session_id, result);

	for (int i = 0; result[i] != '\0'; i++) {
		session_file << result[i];
	}

	session_file.close();
}

/**
 * Assigns a browser ID to the new browser.
 * Determines the correct session ID for the new browser through the interaction with it.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 * @return the ID for the browser
 */
int register_browser(int browser_socket_fd) {
    	int browser_id;

    	for (int i = 0; i < NUM_BROWSER; ++i) {
        	if (!browser_list[i].in_use) {
            	browser_id = i;
		pthread_mutex_lock(&browser_list_mutex);
            	browser_list[browser_id].in_use = true;
            	browser_list[browser_id].socket_fd = browser_socket_fd;
		pthread_mutex_unlock(&browser_list_mutex);
            	break;
        	}
    	}

	char message[BUFFER_LEN];
	receive_message(browser_socket_fd, message);

    	int session_id = strtol(message, NULL, 10);
    	if (session_id == -1) {
		srand((int)time(0));
		session_t session;
		// Get random id
		while (session_id == -1 || session_list.find(session_id) != session_list.end()) {
			session_id = rand() % 10000;
		}
		// Create new session in session_list.
		// Are you supposed to save the sessions created for eternity, and not mark them as unused?
		// If not, you can check sessions used first instead of adding new ones.
		pthread_mutex_lock(&session_list_mutex);
		session_list[session_id] = session;
               	session_list[session_id].in_use = true;
		pthread_mutex_unlock(&session_list_mutex);
    	}

	pthread_mutex_lock(&browser_list_mutex);
    	browser_list[browser_id].session_id = session_id;
	pthread_mutex_unlock(&browser_list_mutex);

    	sprintf(message, "%d", session_id);
    	send_message(browser_socket_fd, message);

    	return browser_id;
}

/**
 * Handles the given browser by listening to it, processing the message received,
 * broadcasting the update to all browsers with the same session ID, and backing up
 * the session on the disk.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 */
void * browser_handler(void* bs_fd) {
	int browser_socket_fd = *((int *) bs_fd);
    	int browser_id = register_browser(browser_socket_fd);

    int socket_fd = browser_list[browser_id].socket_fd;
    int session_id = browser_list[browser_id].session_id;

    printf("Successfully accepted Browser #%d for Session #%d.\n", browser_id, session_id);

    while (true) {
        char message[BUFFER_LEN];
        char response[BUFFER_LEN];

        receive_message(socket_fd, message);
        printf("Received message from Browser #%d for Session #%d: %s\n", browser_id, session_id, message);

        if ((strcmp(message, "EXIT") == 0) || (strcmp(message, "exit") == 0)) {
            close(socket_fd);
            pthread_mutex_lock(&browser_list_mutex);
            browser_list[browser_id].in_use = false;
            pthread_mutex_unlock(&browser_list_mutex);
            printf("Browser #%d exited.\n", browser_id);
            return bs_fd;
        }

        if (message[0] == '\0') {
            continue;
        }

        bool data_valid = process_message(session_id, message);

        if (!data_valid) {
            // Send the error message to the browser.
		send_message(browser_socket_fd, "ERROR");
            continue;
        }

        session_to_str(session_id, response);

        broadcast(session_id, response);

        save_session(session_id);
    }
}

/**
 * Starts the server. Sets up the connection, keeps accepting new browsers,
 * and creates handlers for them.
 *
 * @param port the port that the server is running on
 */
void start_server(int port) {
    // Loads every session if there exists one on the disk.
    load_all_sessions();

    // Creates the socket.
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Binds the socket.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    if (bind(server_socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    // Listens to the socket.
    if (listen(server_socket_fd, SOMAXCONN) < 0) {
        perror("Socket listen failed");
        exit(EXIT_FAILURE);
    }
    printf("The server is now listening on port %d.\n", port);

    // Main loop to accept new browsers and creates handlers for them.
	while (true) {
        	struct sockaddr_in browser_address;
        	socklen_t browser_address_len = sizeof(browser_address);
        	int browser_socket_fd = accept(server_socket_fd, (struct sockaddr *) &browser_address, &browser_address_len);
        	if ((browser_socket_fd) < 0) {
	            	perror("Socket accept failed");
        	    	continue;
        	}

        	// Starts the handler for the new browser.
		pthread_t thread_id;
		pthread_create(&thread_id, NULL, &browser_handler, &browser_socket_fd);
    	}

    // Closes the socket.
    close(server_socket_fd);
}

/**
 * The main function for the server.
 *
 * @param argc the number of command-line arguments passed by the user
 * @param argv the array that contains all the arguments
 * @return exit code
 */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc == 1) {
    } else if ((argc == 3)
               && ((strcmp(argv[1], "--port") == 0) || (strcmp(argv[1], "-p") == 0))) {
        port = strtol(argv[2], NULL, 10);

    } else {
        puts("Invalid arguments.");
        exit(EXIT_FAILURE);
    }

    if (port < 1024) {
        puts("Invalid port.");
        exit(EXIT_FAILURE);
    }

    start_server(port);

    exit(EXIT_SUCCESS);
}
