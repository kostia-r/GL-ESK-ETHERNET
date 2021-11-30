#include "main.h"
#include "lwip.h"
#include "sockets.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdbool.h>

#define PORTNUM 5678UL
#define PORTNUM2 1234UL

/*
 * TODO Define one more port number
 * For example:
 * #define PORTNUM2 1234UL
 */

#define CMD_BUFFER_MAX_LEN 32U

#if (USE_UDP_SERVER_PRINTF == 1)
#include <stdio.h>
#define UDP_SERVER_PRINTF(...) do { printf("[udp_server.c: %s: %d]: ",__func__, __LINE__);printf(__VA_ARGS__); } while (0)
#else
#define UDP_SERVER_PRINTF(...)
#endif

static struct sockaddr_in serv_addr, client_addr, client2_addr;
static int socket_fd, socket2_fd;
/*
 * TODO Declare additional variables
 * For example:
 * static struct sockaddr_in client2_addr;
 * static int socket2_fd;
 */

static int udpServerInit(uint16_t portnum)
{
	uint16_t port;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1)
	{
		UDP_SERVER_PRINTF("socket() error\n");
		return -1;
	}

	port = htons((uint16_t )portnum);

	bzero(&serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = port;

	if (bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
	{
		UDP_SERVER_PRINTF("bind() error\n");
		close(fd);
		return -1;
	}

	UDP_SERVER_PRINTF("UDP Server is bound to port %d\n", portnum);

	return fd;
}

typedef enum
{
	COMMAND_UNKNOWN_ERROR = -1,
	COMMAND_ERR_WRONG_FORMAT = -2,
	COMMAND_ERR_LED_NUMBER = -3,
	COMMAND_ERR_LED_CMD = -4,
	COMMAND_ERR_GPIO_NAME = -5,
	COMMAND_ERR_GPIO_PIN = -6,
	COMMAND_ERR_ARGUMENT = -7,
	COMMAND_OK = 0
} command_error_t;

static command_error_t led_command_handler(const uint8_t *buffer, size_t len)
{
	int num;
	char cmd[sizeof("toggle")];
	Led_TypeDef led[4] = { LED3, LED4, LED5, LED6 };

	if (buffer == NULL || len == 0 || len > CMD_BUFFER_MAX_LEN)
	{
		return COMMAND_ERR_ARGUMENT;
	}

	if (sscanf((const char*) buffer, "led%d %s", &num, cmd) != 2)
	{
		return COMMAND_ERR_WRONG_FORMAT;
	}
	if (num < 3 || num > 6)
	{
		return COMMAND_ERR_LED_NUMBER;
	}
	if (strncmp("on", cmd, sizeof(cmd)) == 0)
	{
		BSP_LED_On(led[num - 3]);
	}
	else if (strncmp("off", cmd, sizeof(cmd)) == 0)
	{
		BSP_LED_Off(led[num - 3]);
	}
	else if (strncmp("toggle", cmd, sizeof(cmd)) == 0)
	{
		BSP_LED_Toggle(led[num - 3]);
	}
	else
	{
		return COMMAND_ERR_LED_CMD;
	}

	return COMMAND_OK;
}

/*
 * Parameters:
 * buffer - a pointer to the input buffer
 * len - buffer length
 * state - a pointer to the GPIO status that should to be returned
 * pin - a pointer to the PIN number that should to be returned
 **/
static command_error_t gpio_command_handler(const uint8_t *buffer, size_t len, bool *state, uint8_t *pin)
{
	char cmd_gpio[1];
	int pin_num = 0;

	if (buffer == NULL || len == 0 || len > CMD_BUFFER_MAX_LEN || state == NULL || pin == NULL)
	{
		return COMMAND_ERR_ARGUMENT;
	}

	if (sscanf((const char*) buffer, "read gpio%s %d", cmd_gpio, &pin_num) != 2)
	{
		return COMMAND_ERR_WRONG_FORMAT;
	}
	*pin = pin_num;
	if (pin_num < 12 || pin_num > 15)
	{
		return COMMAND_ERR_LED_NUMBER;
	}
	if (cmd_gpio[0] == 'd' && pin_num == 12)
	{
		*state = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_12) != GPIO_PIN_RESET) ? 1 : 0;
	}
	else if (cmd_gpio[0] == 'd' && pin_num == 13)
	{
		*state = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_13) != GPIO_PIN_RESET) ? 1 : 0;
	}
	else if (cmd_gpio[0] == 'd' && pin_num == 14)
	{
		*state = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_14) != GPIO_PIN_RESET) ? 1 : 0;
	}
	else if (cmd_gpio[0] == 'd' && pin_num == 15)
	{
		*state = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_15) != GPIO_PIN_RESET) ? 1 : 0;
	}
	else
	{
		return COMMAND_ERR_LED_CMD;
	}

	return COMMAND_OK;
}

void StartUdpServerTask(void const *argument)
{
	int addr_len, addr2_len;

	osDelay(5000); // wait 5 sec to init lwip stack

	if ((socket_fd = udpServerInit(PORTNUM)) < 0)
	{
		UDP_SERVER_PRINTF("udpServerInit(PORTNUM) error\n");
		return;
	}

	if ((socket2_fd = udpServerInit(PORTNUM2)) < 0) // TODO Add initialization of the second server to receive commands from port 1234
	{
		UDP_SERVER_PRINTF("udpServerInit(PORTNUM2) error\n");
		return;
	}

	for (;;)
	{
		bzero(&client_addr, sizeof(client_addr));
		addr_len = sizeof(client_addr);

		bzero(&client2_addr, sizeof(client2_addr));
		addr2_len = sizeof(client2_addr);

		fd_set rfds;
		struct timeval tv;
		int retval;

		/* Watch stdin (fd 0) to see when it has input. */

		FD_ZERO(&rfds);
		FD_SET(socket_fd, &rfds);

		FD_SET(socket2_fd, &rfds); // TODO add the second socket descriptor

		/* Wait up to five seconds. */

		tv.tv_sec = 5;
		tv.tv_usec = 0;

		retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
		/* Don't rely on the value of tv now! */

		if (retval == -1)
		{
			close(socket_fd);
			break;
		}
		else if (retval)
		{
			uint8_t buffer[CMD_BUFFER_MAX_LEN];
			uint8_t buffer_print[CMD_BUFFER_MAX_LEN];
			const size_t buf_size = sizeof(buffer);
			const size_t buf_print_size = sizeof(buffer_print);
			command_error_t r;
			ssize_t received;

			bool state = 0;
			uint8_t pin = 0;
			if (FD_ISSET(socket_fd, &rfds))
			{
				received = recvfrom(socket_fd, buffer, buf_size, MSG_DONTWAIT, (struct sockaddr* )&client_addr, (socklen_t* )&addr_len);

				if (received > 0)
				{
					if ((r = led_command_handler(buffer, received)) != COMMAND_OK)
					{
						UDP_SERVER_PRINTF("command_handler() returned error code = %d\n", (int )r);
						if (sendto(socket_fd, "error\n", sizeof("error\n"), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len) == -1)
						{
							UDP_SERVER_PRINTF("sendto() returned -1 \n");
						}
					}
					else
					{
						UDP_SERVER_PRINTF("command was handles successfully\n");
						if (sendto(socket_fd, "OK\n", sizeof("OK\n"), MSG_DONTWAIT, (const struct sockaddr *)&client_addr, addr_len) == -1)
						{
							UDP_SERVER_PRINTF("sendto() returned -1 \n");
						}
					}
				}
			}

			if (FD_ISSET(socket2_fd, &rfds)) // TODO Check activity on the second socket descriptor, implement the handler
			{
				received = recvfrom(socket2_fd, buffer, buf_size, MSG_DONTWAIT, (struct sockaddr* )&client2_addr, (socklen_t* )&addr2_len);

				if (received > 0)
				{
					if ((r = gpio_command_handler(buffer, received, &state, &pin)) != COMMAND_OK)
					{
						UDP_SERVER_PRINTF("command_handler() returned error code = %d\n", (int )r);
						if (sendto(socket2_fd, "error\n", sizeof("error\n"), MSG_DONTWAIT, (const struct sockaddr *)&client2_addr, addr2_len) == -1)
						{
							UDP_SERVER_PRINTF("sendto() returned -1 \n");
						}
					}
					else
					{
						UDP_SERVER_PRINTF("command was handles successfully\n");
						sprintf((char*) buffer_print, "GPIOD.%d=%d\n", pin, state);
						if (sendto(socket2_fd, buffer_print, buf_print_size, MSG_DONTWAIT, (const struct sockaddr *)&client2_addr, addr2_len) == -1)
						{
							UDP_SERVER_PRINTF("sendto() returned -1 \n");
						}
					}
				}
			}
		}
		else
		{
			UDP_SERVER_PRINTF("No data within five seconds.\n");
		}
	}
}
