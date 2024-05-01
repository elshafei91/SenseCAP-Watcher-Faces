
#pragma once
#include <regex.h>
#include "uhash.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>




typedef struct {
    char command_name[100]; // Assuming the command name will not exceed 100 characters
    void (*func)(char *params);     // Function pointer to the function that will process the command
    UT_hash_handle hh;      // Makes this structure hashable
}command_entry;

#define DEBUG_AT_CMD

#ifdef DEBUG_AT_CMD

#define TASK_STATS_BUFFER_SIZE 50

extern char * test_strings[];
extern command_entry *commands;
void vTaskMonitor(void *para);
#endif







void AT_command_reg();      //  Function to register the AT commands
void AT_command_free();     // Function to free the memory allocated for the commands

void add_command(command_entry ** commands,const char *name ,void (*func)(char *params));   // Function to add a command to the list of commands
void exec_command(command_entry **commands, const char *name,char *params);                 // Function to execute a command

void task_handle_AT_command(); // Function to handle the AT command select witch command to execute


void handle_type_1_command(char *params);   //test command
void handle_device_command(char *params);   //device info
void handle_wifi_command(char *params);     //wifi command
void handle_token(char *params);            //token command
void handle_eui_command(char *params);      //eui command