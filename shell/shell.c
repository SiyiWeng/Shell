/**
 * Shell
 * CS 241 - Fall 2019
 */
#include "format.h"
#include "shell.h"
#include "vector.h"
#include "unistd.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include "sstring.h"
#include <dirent.h>

typedef struct process {
    char *command;
    pid_t pid;
} process;

static vector* my_vector;
static char* path;
static int flag = 0;
static int background = 0;
static pid_t current_pid = 0;
static vector* process_vec;

//helper functions

process* create_process(char* new_command, pid_t new_pid){
    process* my_process = calloc(sizeof(process),1);
    my_process->pid = new_pid;
    my_process->command = new_command;
    return my_process;
}


char **strsplit(char* command, char delim) {
  sstring* sstring = cstr_to_sstring(command); //freed
  vector* args = sstring_split(sstring, delim); //freed
  sstring = NULL;
  char** arr = malloc(sizeof(char*)*(vector_size(args)+1)); //free later
  for (size_t i = 0; i < vector_size(args); i++) {
    arr[i] = strdup(vector_get(args, i));
  }
  arr[vector_size(args)] = NULL;
  return arr;
}


void push_command_history(char* command) {
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }
    vector_push_back(my_vector, command);
}

void history_handle(){
    for (size_t i = 0; i < vector_size(my_vector); i++) {
        print_history_line(i, vector_get(my_vector,i));
    }
}


vector* load_file(char* filename) {
    FILE* stream = fopen(get_full_path(filename), "r");
    if (stream == NULL) {
        print_script_file_error();
    }
    vector* file_vector = string_vector_create();
    char* buffer = NULL;
    size_t capacity = 0;
    ssize_t result = 0;
    while((result = getline(&buffer, &capacity, stream)) != -1) {
        if (buffer[strlen(buffer) - 1] == '\n'){
            buffer[strlen(buffer) - 1] = 0;
        }
        vector_push_back(file_vector, buffer);
    }
    fclose(stream);
    free(buffer);
    return file_vector;
}


void load_in_history_file(char *filename){
   FILE* stream = fopen(get_full_path(filename), "r");
   path = get_full_path(filename);
    if (stream == NULL) {
        print_history_file_error();
        stream = fopen (filename, "w+");
        return;
    } else {
        char* buffer = NULL;
        size_t length = 0;
        while(getline(&buffer, &length, stream) != -1){
            if (buffer[strlen(buffer) - 1] == '\n'){
                buffer[strlen(buffer) - 1] = 0;
            }   
        vector_push_back(my_vector, buffer);
      }

      free(buffer);
      fclose(stream);
      return;
   }
}

void process_kill(pid_t pid) {
    for (size_t i = 0; i < vector_size(process_vec); i++) {
        process *temp = (process *) vector_get(process_vec, i);
        if (temp->pid == pid) {
            vector_erase(process_vec, i);
            break;
        }
    } 
}

int cd_handle(char* command) {
    char** command_str = strsplit(command, ' ');
    if (command[strlen(command) - 1] == '\n') {
        command[strlen(command) - 1] = 0;
    }
    
    char* path = command_str[1];
    if (path[strlen(path)-1] == '\n') {
        path[strlen(path)-1] = 0;
    }

    if (chdir(path) != 0 || !strcmp(path,"")) {
        print_no_directory(path);
        return -1;
    } 
    return 0;
}

int external_handle(char* command){
    if (command[strlen(command) - 1] == '\n') {
        command[strlen(command) - 1] = 0;
    }
    
    if (command[strlen(command) - 1] == '&') {
        background = 1;
        if (command[strlen(command) - 2] == ' ') {
            command[strlen(command) - 1] = 0;
        }
        command[strlen(command) - 1] = 0;
    }
    char* temp = strdup(command);
    
    char** splitstring = strsplit(command, ' ');
    if (!strcmp(splitstring[0], "cd")) {
        return cd_handle(command);
    }

    if (background){
        background = 0;
        // printf("in the background handle\n");
        pid_t pid = fork();
        
        if (pid < 0) {
            print_fork_failed();
            return -3;
        } else if (pid > 0) {
            process* new_process = create_process(temp, pid);
            vector_push_back(process_vec, new_process);

            if (setpgid(pid, pid) == -1) { // failed
                print_setpgid_failed();
                exit(1);
            }
            return 0;
        } else if (pid == 0) {
            print_command_executed(getpid());
            execvp(splitstring[0], &splitstring[0]);
            printf("exe : %s\n", splitstring[0]);
            printf("elijlij: %s\n", splitstring[1]);
            print_exec_failed(command);
            kill(getpid(), SIGTERM);
            return -1;
        }
    } else {
        pid_t pid = fork();
        
        if (pid == -1) {
            print_fork_failed();
            return -3;
        }else if (pid == 0) {
            //child
            current_pid = pid;
            print_command_executed(getpid());
            execvp(splitstring[0], &splitstring[0]);
            print_exec_failed(command);
            kill(getpid(), SIGTERM);
            return -1;
        } else {
            //parent
            process* new_process = create_process(temp, pid);
            vector_push_back(process_vec, new_process);
            if (setpgid(pid, getpid()) == -1) {
                print_setpgid_failed();
                exit(1);
            }
            int status;
            waitpid(pid, &status, 0);
            process_kill(pid);
            if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
                print_wait_failed();
                return -2;
            }
            return 0;
        }
    }
    return 0;
}

void operator_and(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }
    
    sstring* my_sstr = cstr_to_sstring(command);
    vector* answer = string_vector_create();
    if (sstring_substitute(my_sstr, 0, " && ", "*") == 0){
        answer = sstring_split(my_sstr, '*');
    }
 
    if (external_handle(vector_get(answer, 0))==0){
        external_handle(vector_get(answer, 1));
    } else{

    }   
}

void operator_or(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }

    sstring* my_sstr = cstr_to_sstring(command);
    vector* answer = string_vector_create();
    if (sstring_substitute(my_sstr, 0, " || ", "*") == 0){
        answer = sstring_split(my_sstr, '*');
    }
 
    if (external_handle(vector_get(answer, 0))==0){
        
    } else{
        external_handle(vector_get(answer, 1));
    }   
}

void operator_separator(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }
    sstring* my_sstr = cstr_to_sstring(command);
    vector* answer = string_vector_create();
    if (sstring_substitute(my_sstr, 0, "; ", "*") == 0){
        answer = sstring_split(my_sstr, '*');
    }
    external_handle(vector_get(answer, 0));
    external_handle(vector_get(answer, 1));
}

process_info* create_proc_info(process* proc){
    process_info* info = malloc(sizeof(process_info));
    char path[50];
    snprintf(path, 50, "/proc/%d/status", proc->pid);
    FILE *stream = fopen(path,"r");
    if (!stream) {
        print_script_file_error();
        return NULL;
    }
    char* buffer = NULL;
    size_t capacity = 0;
    ssize_t result = 0;
    info->pid = proc->pid;
    info->start_str = NULL;
    info->time_str = NULL;
    info->command = proc->command;
    while((result = getline(&buffer, &capacity, stream)) != -1) {
        char** split_line = strsplit(buffer, ':');
        if (!strcmp(split_line[0], "State")) {
            while(isspace(*split_line[1])) split_line[1]++;
            info -> state = *split_line[1];
        } else if (!strcmp(split_line[0], "Threads")) {
            while(isspace(*split_line[1])) split_line[1]++;
            info -> nthreads = atol(split_line[1]);
        } else if (!strcmp(split_line[0], "VmSize")) {
            while(isspace(*split_line[1])) split_line[1]++;
            info -> vsize = atol(split_line[1]);
        }
    }
    fclose(stream);

    ////////////////////////////////////////////////////////////////
    snprintf(path, 40, "/proc/%d/stat", proc->pid);
    FILE *stat = fopen(path,"r");
    if (!stat) {
      print_script_file_error();
      return NULL;
    }
    unsigned long utime = 0;
    unsigned long stime = 0;
    unsigned long starttime = 0;
    unsigned long btime = 0;
    
    while ((result = getline(&buffer, &capacity, stat)) != -1) {
        char* buffer_copy = strdup(buffer);
        char* temp = strtok(buffer_copy, " ");
        int count = 1;
        while (temp != NULL) {
            if (count == 14) {
                utime = atol(temp);
            } else if (count == 15) {
                stime = atol(temp);
            } else if (count == 22) {
                starttime = atol(temp);
            }
            temp = strtok (NULL, " ");
            count++;
        }
    }
    fclose(stat);
    //////////////////////////////////////////////////////////////////////
    FILE *proc_stat = fopen("/proc/stat","r");
    if (!proc_stat) {
        print_script_file_error();
        return NULL;
    }
    
    while ((result = getline(&buffer, &capacity, proc_stat)) != -1) {
        if(!strncmp(buffer, "btime", 5)) {
            char* temp = buffer + 6;
            while(isspace(*temp)) ++temp;
            btime = atol(temp);
        } 
    }
    fclose(proc_stat);

    // fprintf(stderr, "utime:%lu\n", utime);
    // fprintf(stderr, "stime:%lu\n", stime);
    // fprintf(stderr, "starttime:%llu\n", starttime);
    // fprintf(stderr, "btime:%lu\n", btime);
    // handle time_str
    unsigned long running_time = (utime + stime) / sysconf(_SC_CLK_TCK);
    char* temp1 = malloc(50);
    execution_time_to_string(temp1, 50, running_time / 60, running_time % 60);
    info->time_str = temp1;
    // handle start_str
    time_t total_second_since = starttime/sysconf(_SC_CLK_TCK) + btime;
    struct tm *starting_time = localtime(&total_second_since);
    char* temp2 = malloc(20);
    time_struct_to_string(temp2, 20, starting_time);
    info->start_str = temp2;
    
    return info;
}

void ps_handle(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }

    print_process_info_header();
    for (size_t i = 0; i < vector_size(process_vec); i++) {
        print_process_info(create_proc_info((process*) vector_get(process_vec, i)));
    }   
}


void pfd_handle(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }

    print_process_fd_info_header();
    char** split_command = strsplit(command, ' ');
    pid_t aimpid = atoi(split_command[1]);
    

    size_t fd_no = 0;
    size_t file_pos = 0;
    char realpath[200];  
    char path[50];
    for (size_t i = 0; i < vector_size(process_vec); i++) {
        if (((process*) vector_get(process_vec, i))->pid == aimpid){
            snprintf(path, 50, "/proc/%d/fdinfo", aimpid);
            DIR *dir = opendir(path);
            if (!dir) {
                print_script_file_error();
                return;
            }
            struct dirent *dp;
            while ((dp = readdir(dir)) != NULL) {
                size_t fd_read;
                fd_read = sscanf(dp->d_name, "%zu", &fd_no);

                // fd_info
                if (fd_read) {
                    snprintf(path, 50, "/proc/%d/fdinfo/%zu", aimpid, fd_no);
                    FILE *fdpos_stream = fopen(path,"r");
                    if (!fdpos_stream) {
                        print_script_file_error();
                        return;
                    }
                    // file_pos
                    char* buffer = NULL;
                    size_t capacity = 0;
                    ssize_t result = 0;
                    char* pos;
                    while((result = getline(&buffer, &capacity, fdpos_stream)) != -1) {
                        if (!strncmp(buffer, "pos:", 4)) {
                            pos = buffer + 4;
                            while (isspace(*buffer)) {++pos;}
                            file_pos = atol(pos);
                        }
                    }
                    fclose(fdpos_stream);
                          
                    ssize_t read_answer;
                    snprintf(path, 50, "/proc/%d/fd/%zu", aimpid, fd_no);
                    if((read_answer = readlink(path, realpath, 200)) == -1) {
                        return;
                    }
                    realpath[read_answer] = '\0';
                    print_process_fd_info(fd_no, file_pos, realpath);
                }
            }
            return;
        }
    }
    print_no_process_found(aimpid);
    return;
}

void stop_handle(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }
    char** split_command = strsplit(command, ' ');
    pid_t aimpid = atoi(split_command[1]);
    if (kill(aimpid,0) != -1) {
        ssize_t answer = kill(aimpid, SIGTSTP);
        if (answer == -1) {
            print_no_process_found(aimpid);
            return;
        } else {
            for (size_t i = 0; i < vector_size(process_vec);i++){
                if (((process*) vector_get(process_vec, i))->pid == aimpid){
                    print_stopped_process(aimpid, ((process*) vector_get(process_vec, i))->command);
                    return;
                }
            }
        }
    }
    return;
}

void cont_handle(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }
    char** split_command = strsplit(command, ' ');
    pid_t aimpid = atoi(split_command[1]);
     if (kill(aimpid,0) != -1) {
        ssize_t answer = kill(aimpid, SIGCONT);
        if (answer == -1) {
            print_no_process_found(aimpid);
            return;
        } else {
            for (size_t i = 0; i < vector_size(process_vec);i++){
                if (((process*) vector_get(process_vec, i))->pid == aimpid){
                    return;
                }
            }
        }
    }
    return;
}

void kill_handle(char* command){
    if (command[strlen(command) - 1] == '\n'){
        command[strlen(command) - 1] = 0;
    }
    char** split_command = strsplit(command, ' ');
    pid_t aimpid = atoi(split_command[1]);
     if (kill(aimpid,0) != -1) {
        ssize_t answer = kill(aimpid, SIGTERM);
        if (answer == -1) {
            print_no_process_found(aimpid);
            return;
        } else {
            for (size_t i = 0; i < vector_size(process_vec);i++){
                if (((process*) vector_get(process_vec, i))->pid == aimpid){
                    print_killed_process(aimpid, ((process*) vector_get(process_vec, i))->command);
                    return;
                }
            }
        }
    }
    return;
}

// delete the zombie children
void cleanup() {
    int status;
    pid_t pid;
    while ((pid = waitpid((pid_t)(-1), &status, WNOHANG)) >0) {
        process_kill(pid);
    }
}


void control_c(){
    for (size_t i = 0; i < vector_size(process_vec); i++) {
        process* proc = vector_get(process_vec, i);
        if ( proc->pid != getpgid(proc->pid) ){
            kill(proc->pid, SIGKILL);
        }
    }    
}


void number_handle(char* command){
    command[strlen(command) - 1] = 0;

    size_t iter = 1;
    while(iter != strlen(command)) {
        if (!isdigit(command[iter])){
            print_invalid_command(command);
            return;
        }
        iter++;
    }

    size_t num = atoi(command + 1);
    if (num >= vector_size(my_vector)){
        print_invalid_index();
        return;
    }


    char* new_command = vector_get(my_vector, num);
    push_command_history(new_command);
    printf("%s\n", new_command);

    if (strstr(new_command, " && ")) {
        operator_and(new_command);
    } else if (strstr(new_command, " || ")){
        operator_or(new_command);
    } else if (strstr(new_command, "; ")) {
        operator_separator(new_command);
    } else {
        external_handle(new_command);
    }
    
    return;
}

void prefix_handle(char* command){
    command[strlen(command) - 1] = 0;
    char* target = command + 1;
    char* aim_command = NULL;
    if (!strcmp(command, "!")){
        aim_command = vector_get(my_vector, vector_size(my_vector) - 1);
    } else {
        for(int i = vector_size(my_vector) - 1; i >= 0; i--) {
            if (!strstr(vector_get(my_vector, i), target)) {
                continue;
            } else {
                aim_command = vector_get(my_vector, i);
                break;
            }
        }
    }
    
    if (!aim_command) {
        print_no_history_match();
        return;
    }
    push_command_history(aim_command);
    printf("%s\n", aim_command);

    if (strstr(aim_command, " && ")) {
        operator_and(aim_command);
    } else if (strstr(aim_command, " || ")){
        operator_or(aim_command);
    } else if (strstr(aim_command, "; ")) {
        operator_separator(aim_command);
    } else {
        external_handle(aim_command);
    }
    return;
}


int all_commands(char* buffer) {
    char cwd[80];
    if (getcwd(cwd, sizeof(cwd))) {
            print_prompt(getcwd(cwd, sizeof(cwd)), getpid());
            printf("%s\n", buffer);
    }
    char** split_buffer = strsplit(buffer, ' ');
        if (strstr(buffer, " && ")) {
            operator_and(buffer);
            push_command_history(buffer);
        } else if (strstr(buffer, " || ")){
            operator_or(buffer);
            push_command_history(buffer);
        } else if (strstr(buffer, "; ")) {
            operator_separator(buffer);
            push_command_history(buffer);
        } else if (strcmp(buffer, "ps\n") == 0) {
            ps_handle(buffer);
            push_command_history(buffer);
        } else if (strcmp(split_buffer[0], "cd") == 0) {
            cd_handle(buffer);
            push_command_history(buffer);
        } else if (strcmp(buffer, "!history") == 0) {
            history_handle();
        } else if (buffer[0] == '#') {
            number_handle(buffer);
        } else if (strcmp(buffer, "!history") && buffer[0] == '!') {
            prefix_handle(buffer);
        } else {
            if(external_handle(buffer) == 0){
                push_command_history(buffer);
            } else {
                push_command_history(buffer);
                print_invalid_command(buffer);
            }
        }
        return 0;
}

int shell(int argc, char *argv[]) {
    // TODO: This is the entry point for your shell.
    signal(SIGCHLD, cleanup);
    signal(SIGINT, control_c);
    my_vector = string_vector_create();
    process_vec = shallow_vector_create();

    if (argc == 1) {
        process* new_process = create_process(argv[0], getpid());
        vector_push_back(process_vec, new_process);
    }
    int opt;
    
    while((opt = getopt(argc, argv, "f:h:")) != -1) {    
        switch(opt)  {  
            case 'f': {
                vector* file_vec = load_file(optarg);
                for(size_t i = 0; i < vector_size(file_vec); i++) {
                    all_commands(vector_get(file_vec, i));
                }
                return 0;
            }
            case 'h': {
                load_in_history_file(optarg);
                flag = 1;
            }
        }  
    }  
    
    char* buffer = NULL;
    size_t capacity = 0;
    char cwd[80];
    if (getcwd(cwd, sizeof(cwd))) {
        print_prompt(getcwd(cwd, sizeof(cwd)), getpid());
    }
    
    while(getline(&buffer, &capacity, stdin) != -1) {
        char** split_buffer = strsplit(buffer, ' ');
        if (strstr(buffer, " && ")) {
            operator_and(buffer);
            push_command_history(buffer);
        } else if (strstr(buffer, " || ")){
            operator_or(buffer);
            push_command_history(buffer);
        } else if (strstr(buffer, "; ")) {
            operator_separator(buffer);
            push_command_history(buffer);
        } else if (strcmp(split_buffer[0], "cd") == 0) {
            cd_handle(buffer);
            push_command_history(buffer);
        } else if (strcmp(buffer, "!history\n") == 0) {
            history_handle();
        } else if (buffer[0] == '#') {
            number_handle(buffer);
        } else if (strcmp(buffer, "!history\n") && buffer[0] == '!') {
            prefix_handle(buffer);
        } else if (strcmp(buffer, "exit\n") == 0) {
            break;
        } else if (strcmp(buffer, "ps\n") == 0) {
            ps_handle(buffer);
            push_command_history(buffer);
        } else if (strcmp(split_buffer[0], "kill")==0){
            kill_handle(buffer);
            push_command_history(buffer);
        } else if (strcmp(split_buffer[0], "pfd") == 0){
            pfd_handle(buffer);
            push_command_history(buffer);
        }else if (strcmp(split_buffer[0], "stop") == 0){
            stop_handle(buffer);
            push_command_history(buffer);
        } else if (strcmp(split_buffer[0], "cont")==0){
            cont_handle(buffer);
            push_command_history(buffer);
        } else {
            if(external_handle(buffer) == 0){
                push_command_history(buffer);
            } else {
                push_command_history(buffer);
                print_invalid_command(buffer);
            }
        }
        
        if (getcwd(cwd, sizeof(cwd))) {
            print_prompt(getcwd(cwd, sizeof(cwd)), getpid());
        }
        
        
        if (flag) {
            FILE* stream = fopen(path, "w");
            for(size_t i = 0; i < vector_size(my_vector); i++){
                fprintf(stream, "%s\n", vector_get(my_vector, i));
            }
        }

    }
    return 0;
}


