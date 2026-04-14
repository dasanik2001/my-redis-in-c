#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#define BUF_SIZE 1024
#define MAX_MAP_SIZE 100
struct entry
{
	char *key;
	char *value;
};
struct server_data
{
	struct entry *entries;
	int numOfElements;
};
void set(struct server_data *sd, char *key, char *value)
{
	// Update value if key already exists
	for (int i = 0; i < sd->numOfElements; i++)
	{
		if (strcmp(sd->entries[i].key, key) == 0)
		{
			sd->entries[i].value = value;
			return;
		}
	}

	// Grow the entries array by 1 and append new entry
	sd->entries = realloc(sd->entries, sizeof(struct entry) * (sd->numOfElements + 1));
	sd->entries[sd->numOfElements].key = key;
	sd->entries[sd->numOfElements].value = value;
	sd->numOfElements++;
}
char *get(struct server_data *sd, char *key)
{
	for (int i = 0; i < sd->numOfElements; i++)
	{
		if (strcmp(sd->entries[i].key, key) == 0)
		{
			return sd->entries[i].value;
		}
	}
	return "Key not found!";
}
// Function to parse RESP commands and generate appropriate responses
// example: *2\r\n$4\r\nECHO\r\n$5\r\napple\r\n
char *resp_parser(char *buff)
{
	char *bulk_response;
	bulk_response = malloc(BUF_SIZE);
	if (buff[0] != '*')
		return NULL; // Empty check -> if NULL continue
	char *ptr = buff;

	ptr = strchr(ptr, '\r');
	if (!ptr)
		return NULL;

	ptr = ptr + 2; // if \r is there skip next 2 characters \n

	// Read command
	if (*ptr != '$')
		return NULL; // if $ is not there return NULL

	int command_length = atoi(ptr + 1);
	ptr = strchr(ptr, '\r');
	if (!ptr)
		return NULL;
	ptr = ptr + 2; // if \r is there skip next 2 characters \n

	char command[32];
	strncpy(command, ptr, command_length);
	command[command_length] = '\0'; // Null-terminate the command string

	for (int i = 0; command[i]; i++)
	{
		command[i] = toupper(command[i]);
	}

	ptr += command_length;
	ptr += 2; // Skip \r\n

	const char *response = NULL;
	int response_len = 0;

	if (strcmp(command, "PING") == 0)
	{
		return "+PONG\r\n";
	}
	else if (strcmp(command, "ECHO") == 0)
	{
		if (*ptr != '$')
			return NULL; // if $ is not there return NULL
		int echo_length = atoi(ptr + 1);
		ptr = strchr(ptr, '\r');
		if (!ptr)
			return NULL;
		ptr = ptr + 2; // if \r is there skip next 2 characters \n

		// Build response
		int offset = sprintf(bulk_response, "$%d\r\n", echo_length);
		memcpy(bulk_response + offset, ptr, echo_length);
		offset += echo_length;
		strcpy(bulk_response + offset, "\r\n");
		return bulk_response;
	}
	else if (strcmp(command, "SET") == 0)
	{
		struct server_data *sd = malloc(sizeof(struct server_data));

		// Parse key
		if (*ptr != '$')
			return NULL;
		int key_length = atoi(ptr + 1);
		ptr = strchr(ptr, '\r');
		if (!ptr)
			return NULL;
		ptr += 2;

		char *key = malloc(key_length + 1);
		strncpy(key, ptr, key_length);
		key[key_length] = '\0';
		ptr += key_length + 2; // skip \r\n

		// Parse value
		if (*ptr != '$')
			return NULL;
		int value_length = atoi(ptr + 1);
		ptr = strchr(ptr, '\r');
		if (!ptr)
			return NULL;
		ptr += 2;

		char *value = malloc(value_length + 1);
		strncpy(value, ptr, value_length);
		value[value_length] = '\0';

		set(sd, key, value);
		return "+OK\r\n";
	}
	else if (strcmp(command, "GET") == 0)
	{
		struct server_data *sd = malloc(sizeof(struct server_data));
		// Parse key
		if (*ptr != '$')
			return NULL;
		int key_length = atoi(ptr + 1);
		ptr = strchr(ptr, '\r');
		if (!ptr)
			return NULL;
		ptr += 2;

		char key[64];
		strncpy(key, ptr, key_length);
		key[key_length] = '\0';

		char *value = get(sd);
		if (strcmp(value, "Key not found!") == 0)
		{
			return "$-1\r\n"; // RESP null bulk string for missing key
		}

		int offset = sprintf(bulk_response, "$%d\r\n", (int)strlen(value));
		memcpy(bulk_response + offset, value, strlen(value));
		offset += strlen(value);
		strcpy(bulk_response + offset, "\r\n");
		return bulk_response;
	}

	return NULL;
}

int main()
{
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment the code below to pass the first stage
	//
	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(6379),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0)
	{
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);

	while (1)
	{
		// Keep on accepting connections
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		// Check if accept is successfull
		if (client_fd == -1)
		{
			printf("Accept failed: %s \n");
			close(server_fd);
			return 1;
		}
		printf("Client connected\n");

		pid_t pid = fork();
		assert(pid != -1); // Fork Failed
		if (pid == 0)
		{
			close(server_fd);
			// Child process to handle client connections
			char buff[BUF_SIZE];
			int bytes_read;
			while ((bytes_read = read(client_fd, buff, BUF_SIZE)) > 0)
			// Can use recv(client_fd, buff, BUF_SIZE,0) as well
			{
				buff[bytes_read] = '\0';
				char *buff_response = resp_parser(buff);
				if (buff_response == NULL)
					continue;
				else
				{
					send(client_fd, buff_response, strlen(buff_response), 0);
				}
				// Until Read is not null keep on writing to FD
				// write(client_fd, response,  strlen(response));

				// Both ways works
			}
			// printf("%d", strlen(response));
			// if (send(client_fd, response, strlen(response), 0) == -1) {
			// 	// If not able to send
			// 	printf("Send failed: %s \n", strerror(errno));
			// }
			close(client_fd);
			exit(0);
		}
	}
	close(server_fd);
	return 0;
}
