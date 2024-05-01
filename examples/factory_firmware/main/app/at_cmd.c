
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <regex.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"


#include "uhash.h"
#include "at_cmd.h"
#include "cJSON.h"


#ifdef DEBUG_AT_CMD
    char *test_strings[] = {
            "\rAT+type1?\n",
            "\rAT+wifi={\"Ssid\":\"Watcher_Wifi\",\"Password\":\"12345678\"}\n",
            "\rAT+type3\n", // Added a test string without parameters
            NULL
    };
    // array to hold task status
    TaskStatus_t pxTaskStatusArray[TASK_STATS_BUFFER_SIZE];
    // task number
    UBaseType_t uxArraySize, x;
    // total run time
    uint32_t ulTotalRunTime;
#endif

//const char *pattern = "^\rAT\\+([^?=]+)(\\?|=([^\\n]*))?\n$"; // Made the parameters part optional
const char *pattern = "^\rAT\\+([^?=]+)(\\?|=([^\\n]*))?\n$"; // Made the parameters part optional

command_entry *commands = NULL;  // Global variable to store the commands




void add_command(command_entry ** commands,const char *name ,void (*func)(char *params)) {
    command_entry *entry = (command_entry *)malloc(sizeof(command_entry));  // Allocate memory for the new entry
    strcpy(entry->command_name, name);  // Copy the command name to the new entry
    entry->func = func; // Assign the function pointer to the new entry
    HASH_ADD_STR(*commands, command_name, entry);   // Add the new entry to the hash table
}
void exec_command(command_entry **commands, const char *name,char *params) {
    command_entry *entry;
    printf("command name is:%s\n",name);
    HASH_FIND_STR(*commands, name, entry);  // Find the entry with the given name
    if(entry) {
        entry->func(params);  // Execute the function if the entry is found
    } else {
        printf("Command not found\n");
    }
}



void AT_command_reg(){  // Register the AT commands
    add_command(&commands, "type1", handle_type_1_command);
    add_command(&commands, "device", handle_device_command);
    add_command(&commands, "wifi", handle_wifi_command);
    add_command(&commands, "eui", handle_eui_command);
    add_command(&commands, "token", handle_token);
}
void AT_command_free(){
    command_entry *current_command, *tmp;
    HASH_ITER(hh, commands, current_command, tmp) {
        HASH_DEL(commands, current_command);  // Delete the entry from the hash table
        free(current_command);  // Free the memory allocated for the entry
    }
}



void handle_type_1_command(char *params) {
    printf("Handling type 1 command\n");
    printf("Params: %s\n", params);
}


void handle_device_command(char *params) {
    printf("Handling device command\n");
}


void handle_wifi_command(char *params) {
    char ssid[100];
    char password[100];
    cJSON *json = cJSON_Parse(params);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
    }
    cJSON *json_ssid = cJSON_GetObjectItemCaseSensitive(json, "Ssid");
    cJSON *json_password = cJSON_GetObjectItemCaseSensitive(json, "Password");
    if (cJSON_IsString(json_ssid) && (json_ssid->valuestring != NULL)) {
        strncpy(ssid, json_ssid->valuestring, sizeof(ssid));
        printf("SSID in json: %s\n", ssid);
    } else {
        printf("SSID not found in JSON\n");
    }

    if (cJSON_IsString(json_password) && (json_password->valuestring != NULL)) {
        strncpy(password, json_password->valuestring, sizeof(password));
        printf("Password in json : %s\n", password);
    } else {
        printf("Password not found in JSON\n");
    }
    printf("Handling wifi command\n");
}



void handle_token(char *params){
    printf("Handling token command\n");
}


void handle_eui_command(char *params) {
    printf("Handling eui command\n");
}


void task_handle_AT_command_old()
{
    regex_t regex;
    int ret;
    char msgbuf[100];  // error buffer
    regmatch_t matches[2];  // matched strings
   
    
    while (1)
    {
    AT_command_reg();               //register the AT commands
    // compile regular expression
    ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret) {
        fprintf(stderr, "Could not compile regex\n");
        return; // if compilation failed, return
    }

    // execuate the regex
    int i=0;
    while(test_strings[i]!=NULL){
    ret = regexec(&regex, test_strings[i], 4, matches, 0);
    if (!ret) {
        char paramBuffer[100]="";
        char typeBuffer[100] = "";
        printf("Match found\n");
        if (matches[1].rm_so != -1) {
            int length = matches[1].rm_eo - matches[1].rm_so;
            strncpy(typeBuffer, test_strings[i] + matches[1].rm_so, length);
            typeBuffer[length] = '\0';
            printf("Type: %s\n", typeBuffer);
        }
        if (matches[3].rm_so != -1) {
            //int length = matches[3].rm_eo - matches[3].rm_so;
            snprintf(paramBuffer, sizeof(paramBuffer), "%.*s",
                    (int)(matches[3].rm_eo - matches[3].rm_so),
                        test_strings[i] + matches[3].rm_so);
            printf("Parameter: %s\n", paramBuffer);
        }
        exec_command(&commands, typeBuffer,paramBuffer);
    } else if (ret == REG_NOMATCH) {
        printf("No match found\n");
    } else {
        regerror(ret, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
    }
    
    i++;
    }
    regfree(&regex);
    AT_command_free();
    vTaskDelay(1000 / portTICK_PERIOD_MS); // delay 1s
    }
}

void task_handle_AT_command(){
    regex_t regex;
    int ret;
    while(1){
    ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret) {
        printf("Could not compile regex\n");
    }

    char *test_strings[] = {
            "\rAT+type1?\n",
            "\rAT+wifi={\"Ssid\":\"Watcher_Wifi\",\"Password\":\"12345678\"}\n",
            "\rAT+type3\n", // Added a test string without parameters
            NULL
    };
    AT_command_reg();
    int i = 0;
    while (test_strings[i] != NULL) {
        regmatch_t matches[4];
        ret = regexec(&regex, test_strings[i], 4, matches, 0);
        if (!ret) {
            char command_type[20];
            snprintf(command_type, sizeof(command_type), "%.*s",
                    (int)(matches[1].rm_eo - matches[1].rm_so),
                        test_strings[i] + matches[1].rm_so);

            char params[100] = "";
            if (matches[3].rm_so != -1) {
                snprintf(params, sizeof(params), "%.*s",
                        (int)(matches[3].rm_eo - matches[3].rm_so),
                            test_strings[i] + matches[3].rm_so);
            }

            exec_command(&commands, command_type,params);
        } else if (ret == REG_NOMATCH) {
            printf("No match: %s\n", test_strings[i]);
        } else {
            char errbuf[100];
            regerror(ret, &regex, errbuf, sizeof(errbuf));
            printf("Regex match failed: %s\n", errbuf);
        }
        i++;
    }

    regfree(&regex);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS); // delay 1s
    }

}

#ifdef DEBUG_AT_CMD
void vTaskMonitor(void *para)
{

        while(1){
        //  get the number of tasks
        uxArraySize = uxTaskGetNumberOfTasks();

        // make sure the array size is not greater than the buffer size
        if (uxArraySize > TASK_STATS_BUFFER_SIZE)
        {
            uxArraySize = TASK_STATS_BUFFER_SIZE;
        }

        // get the task status
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

        // output the task status
        for (x = 0; x < uxArraySize; x++)
        {
            printf("Task %s:\n\tState: %u\n\tPriority: %u\n\tStack High Water Mark: %lu\n",
                   pxTaskStatusArray[x].pcTaskName,
                   pxTaskStatusArray[x].eCurrentState,
                   pxTaskStatusArray[x].uxCurrentPriority,
                   pxTaskStatusArray[x].usStackHighWaterMark);
        }

        // wait for 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
        }
}
#endif