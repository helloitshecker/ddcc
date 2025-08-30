#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_FILE_PER_PROJECT 1024
#define MAX_LINE_LENGTH 1024
#define MAX_EXECUTABLES 128
#define NIL "Not Provided"

#define BOLD      "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RESET   "\033[0m"

typedef struct { bool debug; char* build_folder; } ArgumentsEnabled;

typedef struct { char* name; char* version; char* description; } DiddyProject;

typedef struct { char* name; char* files[MAX_FILE_PER_PROJECT]; uint32_t file_count; } DiddyExecutable;

typedef struct { DiddyExecutable exes[MAX_EXECUTABLES]; uint32_t count; } DiddyExecutableAtlas;

void trim(char* str) {
	uint32_t i = 0, len = strlen(str);
	while (i < len && str[i] == ' ') i++;
	if (i == 0) return;
	memmove(str, str + i, len - i + 1);
}

ArgumentsEnabled process_args(int argc, char** argv) {
	ArgumentsEnabled args = { .debug = true, .build_folder = "build" };
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-debug")) args.debug = true;
		else if (!strcmp(argv[i], "-release") || !strcmp(argv[i], "-nodebug")) args.debug = false;
		else if (!strcmp(argv[i], "-build") && i + 1 < argc) args.build_folder = argv[++i];
	}
	return args;
}

char* ParseTextAfterEqual(char* line) {
	char text[1024] = {};
	uint32_t counter = 0;
	bool equal = false;
	for (uint32_t i = 0; line[i] != '\n' && line[i] != '\0'; i++) {
		if (line[i] == ' ' && !equal) continue;
		if (line[i] == '=') { equal = true; continue; }
		if (equal && counter < 1023) text[counter++] = line[i];
	}
	text[counter] = '\0';
	trim(text);
	return strdup(text);
}

DiddyProject ParseDiddySectionProject(FILE* file) {
	DiddyProject dd = { .name=NIL, .version="0.0.0", .description=NIL };
	char line[MAX_LINE_LENGTH];
	while (fgets(line, sizeof(line), file)) {
		if (line[0]=='#' || line[0]==' ' || line[0]=='\n') continue;
		if (line[0]=='[') { fseek(file,-strlen(line),SEEK_CUR); break; }
		if (!strncmp(line,"name",4)) dd.name=ParseTextAfterEqual(line);
		else if (!strncmp(line,"version",7)) dd.version=ParseTextAfterEqual(line);
		else if (!strncmp(line,"description",11)) dd.description=ParseTextAfterEqual(line);
	}
	printf(BOLD COLOR_YELLOW "Project Name: %s\nProject Version: %s\nProject Description: %s\n" COLOR_RESET, dd.name, dd.version, dd.description);
	return dd;
}

DiddyExecutable ParseDiddySectionExecutable(FILE* file) {
	DiddyExecutable dd = { .name=NIL, .file_count=0 };
	char line[MAX_LINE_LENGTH], *filesstr=NULL;
	while (fgets(line,sizeof(line),file)){
		if(line[0]=='#'||line[0]==' '||line[0]=='\n') continue;
		if(line[0]=='['){ fseek(file,-strlen(line),SEEK_CUR); break; }
		if(!strncmp(line,"name",4)) dd.name=ParseTextAfterEqual(line);
		else if(!strncmp(line,"files",5)) filesstr=ParseTextAfterEqual(line);
	}
	if(filesstr){
		char* token=strtok(filesstr," ");
		while(token && dd.file_count<MAX_FILE_PER_PROJECT){
			dd.files[dd.file_count++]=strdup(token);
			token=strtok(NULL," ");
		}
		free(filesstr);
	}
	return dd;
}

DiddyProject dd_project={};
DiddyExecutableAtlas dd_exes={ .count=0 };

void ParseDiddy(const char* filename){
	FILE* file=fopen(filename,"r");
	if(!file){ fprintf(stderr,BOLD COLOR_RED "Failed to find diddy file! Exiting!\n" COLOR_RESET); exit(-1);}
	char line[MAX_LINE_LENGTH];
	while(fgets(line,sizeof(line),file)){
		if(line[0]=='#') continue;
		if(!strncmp(line,"[project]",9)) dd_project=ParseDiddySectionProject(file);
		else if(!strncmp(line,"[executable]",12) && dd_exes.count<MAX_EXECUTABLES)
			dd_exes.exes[dd_exes.count++]=ParseDiddySectionExecutable(file);
	}
	fclose(file);
}

char* change_c_to_o(const char* filename){
	size_t len=strlen(filename);
	char* newname=malloc(len+3);
	strcpy(newname,filename);
	char* dot=strrchr(newname,'.');
	if(dot) strcpy(dot,".o"); else strcat(newname,".o");
	return newname;
}

void mkdir_if_not_exists(const char* dir){
	#ifdef _WIN32
	_mkdir(dir);
	#else
	mkdir(dir,0755);
	#endif
}

int main(int argc,char* argv[]){
	printf(BOLD COLOR_YELLOW "Welcome to Diddy Builder Party!\n" COLOR_RESET);
	ArgumentsEnabled args=process_args(argc,argv);
	if(args.debug) printf(BOLD COLOR_YELLOW "Debug is enabled!\n" COLOR_RESET);
	else printf(BOLD COLOR_YELLOW "Debug is disabled!\n" COLOR_RESET);

	ParseDiddy("diddy");
	mkdir_if_not_exists(args.build_folder);
	printf(BOLD COLOR_YELLOW "STARTING COMPILATION...\n" COLOR_RESET);

	for(uint32_t i=0;i<dd_exes.count;i++){
		DiddyExecutable* exe=&dd_exes.exes[i];
		char link_cmd[4096];
		strcpy(link_cmd,"gcc ");
		for(uint32_t j=0;j<exe->file_count;j++){
			char* obj=change_c_to_o(exe->files[j]);
			printf(BOLD COLOR_YELLOW "[Executable:%s] Creating object file \"%s\"\n" COLOR_RESET,exe->name,exe->files[j]);
			char cmd[512];
			snprintf(cmd,sizeof(cmd),"gcc -c %s -o %s/%s",exe->files[j],args.build_folder,obj);
			if(system(cmd)!=0){ fprintf(stderr,BOLD COLOR_RED "Failed to compile file \"%s\"\n" COLOR_RESET,exe->files[j]); free(obj); exit(-1);}
			printf(BOLD COLOR_GREEN "[Executable:%s] Object file \"%s\" created successfully\n" COLOR_RESET, exe->name, obj);
			char temp[256]; snprintf(temp,sizeof(temp)," %s/%s",args.build_folder,obj);
			strcat(link_cmd,temp);
			free(obj);
		}
		strcat(link_cmd," -o ");
		strcat(link_cmd,args.build_folder);
		strcat(link_cmd,"/");
		strcat(link_cmd,exe->name);
		printf(BOLD COLOR_GREEN "[Executable:%s] Linking executable \"%s/%s\"\n" COLOR_RESET,exe->name,args.build_folder,exe->name);
		if(system(link_cmd)!=0){ fprintf(stderr,BOLD COLOR_RED "Failed to link executable \"%s\"\n" COLOR_RESET,exe->name); exit(-1);}
	}

	printf(BOLD COLOR_GREEN "COMPILATION COMPLETE!\n" COLOR_RESET);
}
