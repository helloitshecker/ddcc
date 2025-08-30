#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FILE_PER_PROJECT 1024
#define MAX_LINE_LENGTH 1024
#define MAX_EXECUTABLES 128
#define NIL "Not Provided"
#define MAX_THREADS 8

#define BOLD      "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RESET   "\033[0m"

typedef struct { bool debug; char* build_folder; bool clean; bool verbose; char* output; char* install_path; } ArgumentsEnabled;
typedef struct { char* name; char* version; char* description; } DiddyProject;
typedef struct { char* name; char* files[MAX_FILE_PER_PROJECT]; uint32_t file_count; } DiddyExecutable;
typedef struct { char* name; char* files[MAX_FILE_PER_PROJECT]; uint32_t file_count; } DiddyLibrary;
typedef struct { DiddyExecutable exes[MAX_EXECUTABLES]; uint32_t count; } DiddyExecutableAtlas;
typedef struct { DiddyLibrary libs[MAX_EXECUTABLES]; uint32_t count; } DiddyLibraryAtlas;
typedef struct { char* src; char* build_folder; bool verbose; } CompileTask;

DiddyProject dd_project={};
DiddyExecutableAtlas dd_exes={ .count=0 };
DiddyLibraryAtlas dd_libs={ .count=0 };

void trim(char* str){ uint32_t i=0,len=strlen(str); while(i<len && str[i]==' ') i++; if(i) memmove(str,str+i,len-i+1);}

ArgumentsEnabled process_args(int argc,char** argv){
	ArgumentsEnabled args={.debug=true,.build_folder="build",.clean=false,.verbose=false,.output=NULL,.install_path=NULL};
	for(int i=1;i<argc;i++){
		if(!strcmp(argv[i],"-debug")) args.debug=true;
		else if(!strcmp(argv[i],"-release")||!strcmp(argv[i],"-nodebug")) args.debug=false;
		else if(!strcmp(argv[i],"-build") && i+1<argc) args.build_folder=argv[++i];
		else if(!strcmp(argv[i],"-clean")) args.clean=true;
		else if(!strcmp(argv[i],"-verbose")) args.verbose=true;
		else if(!strcmp(argv[i],"-output") && i+1<argc) args.output=argv[++i];
		else if(!strcmp(argv[i],"-install") && i+1<argc) args.install_path=argv[++i];
		else if(!strcmp(argv[i],"-help")){
			printf(BOLD COLOR_YELLOW "Diddy Builder Help:\n" COLOR_RESET);
			printf(BOLD "  -debug       " COLOR_YELLOW "Enable debug build\n" COLOR_RESET);
			printf(BOLD "  -release     " COLOR_YELLOW "Enable release build\n" COLOR_RESET);
			printf(BOLD "  -build <dir> " COLOR_YELLOW "Specify build folder\n" COLOR_RESET);
			printf(BOLD "  -clean       " COLOR_YELLOW "Clean build folder\n" COLOR_RESET);
			printf(BOLD "  -verbose     " COLOR_YELLOW "Show compilation commands\n" COLOR_RESET);
			printf(BOLD "  -output <name> " COLOR_YELLOW "Override executable name\n" COLOR_RESET);
			printf(BOLD "  -install <path> " COLOR_YELLOW "Copy final executables/libraries to path\n" COLOR_RESET);
			printf(BOLD "  -help        " COLOR_YELLOW "Show this help\n" COLOR_RESET);
			exit(0);
		}
	}
	return args;
}

char* ParseTextAfterEqual(char* line){
	char text[1024]={}; uint32_t counter=0; bool equal=false;
	for(uint32_t i=0;line[i]!='\0'&&line[i]!='\n';i++){
		if(line[i]==' '&&!equal) continue;
		if(line[i]=='='){ equal=true; continue;}
		if(equal&&counter<1023) text[counter++]=line[i];
	}
	text[counter]='\0'; trim(text); return strdup(text);
}

DiddyProject ParseDiddySectionProject(FILE* file){
	DiddyProject dd={.name=NIL,.version="0.0.0",.description=NIL};
	char line[MAX_LINE_LENGTH];
	while(fgets(line,sizeof(line),file)){
		if(line[0]=='#'||line[0]==' '||line[0]=='\n') continue;
		if(line[0]=='['){ fseek(file,-strlen(line),SEEK_CUR); break;}
		if(!strncmp(line,"name",4)) dd.name=ParseTextAfterEqual(line);
		else if(!strncmp(line,"version",7)) dd.version=ParseTextAfterEqual(line);
		else if(!strncmp(line,"description",11)) dd.description=ParseTextAfterEqual(line);
	}
	printf(BOLD COLOR_YELLOW "Project: %s | Version: %s | Description: %s\n" COLOR_RESET,dd.name,dd.version,dd.description);
	return dd;
}

DiddyLibrary ParseDiddySectionLibrary(FILE* file){
	DiddyLibrary lib={.name=NIL,.file_count=0};
	char line[MAX_LINE_LENGTH],*filesstr=NULL;
	while(fgets(line,sizeof(line),file)){
		if(line[0]=='#'||line[0]==' '||line[0]=='\n') continue;
		if(line[0]=='['){ fseek(file,-strlen(line),SEEK_CUR); break;}
		if(!strncmp(line,"name",4)) lib.name=ParseTextAfterEqual(line);
		else if(!strncmp(line,"files",5)) filesstr=ParseTextAfterEqual(line);
	}
	if(filesstr){
		char* token=strtok(filesstr," ");
		while(token && lib.file_count<MAX_FILE_PER_PROJECT){
			lib.files[lib.file_count++]=strdup(token);
			token=strtok(NULL," ");
		}
		free(filesstr);
	}
	return lib;
}

DiddyExecutable ParseDiddySectionExecutable(FILE* file){
	DiddyExecutable exe={.name=NIL,.file_count=0};
	char line[MAX_LINE_LENGTH],*filesstr=NULL;
	while(fgets(line,sizeof(line),file)){
		if(line[0]=='#'||line[0]==' '||line[0]=='\n') continue;
		if(line[0]=='['){ fseek(file,-strlen(line),SEEK_CUR); break;}
		if(!strncmp(line,"name",4)) exe.name=ParseTextAfterEqual(line);
		else if(!strncmp(line,"files",5)) filesstr=ParseTextAfterEqual(line);
	}
	if(filesstr){
		char* token=strtok(filesstr," ");
		while(token && exe.file_count<MAX_FILE_PER_PROJECT){
			exe.files[exe.file_count++]=strdup(token);
			token=strtok(NULL," ");
		}
		free(filesstr);
	}
	return exe;
}

void ParseDiddy(const char* filename){
	FILE* file=fopen(filename,"r");
	if(!file){ fprintf(stderr,BOLD COLOR_RED "Failed to find diddy file!\n" COLOR_RESET); exit(-1);}
	char line[MAX_LINE_LENGTH];
	while(fgets(line,sizeof(line),file)){
		if(line[0]=='#') continue;
		if(!strncmp(line,"[project]",9)) dd_project=ParseDiddySectionProject(file);
		else if(!strncmp(line,"[library]",9)&&dd_libs.count<MAX_EXECUTABLES)
			dd_libs.libs[dd_libs.count++]=ParseDiddySectionLibrary(file);
		else if(!strncmp(line,"[executable]",12)&&dd_exes.count<MAX_EXECUTABLES)
			dd_exes.exes[dd_exes.count++]=ParseDiddySectionExecutable(file);
	}
	fclose(file);
}

char* change_c_to_o(const char* filename){
	size_t len=strlen(filename); char* newname=malloc(len+3);
	strcpy(newname,filename);
	char* dot=strrchr(newname,'.'); if(dot) strcpy(dot,".o"); else strcat(newname,".o");
	return newname;
}

void mkdir_if_not_exists(const char* dir){ struct stat st={0}; if(stat(dir,&st)==-1) mkdir(dir,0755); }

void* compile_file(void* arg){
	CompileTask* t=(CompileTask*)arg;
	char* obj=change_c_to_o(t->src);
	char cmd[1024]; snprintf(cmd,sizeof(cmd),"gcc -c %s -o %s/%s",t->src,t->build_folder,obj);
	if(t->verbose) printf(BOLD COLOR_YELLOW "CMD: %s\n" COLOR_RESET,cmd);
	if(system(cmd)!=0){ fprintf(stderr,BOLD COLOR_RED "Failed to compile %s\n" COLOR_RESET,t->src); free(obj); exit(-1);}
	printf(BOLD COLOR_GREEN "Compiled %s -> %s/%s\n" COLOR_RESET,t->src,t->build_folder,obj);
	free(obj);
	return NULL;
}

void compile_library(DiddyLibrary* lib,const char* build_folder,bool verbose){
	pthread_t threads[MAX_FILE_PER_PROJECT];
	CompileTask tasks[MAX_FILE_PER_PROJECT];
	mkdir_if_not_exists(build_folder);
	for(uint32_t i=0;i<lib->file_count;i++){
		tasks[i].src=lib->files[i];
		tasks[i].build_folder=(char*)build_folder;
		tasks[i].verbose=verbose;
		pthread_create(&threads[i%MAX_THREADS],NULL,compile_file,&tasks[i]);
		if(i%MAX_THREADS==MAX_THREADS-1||i==lib->file_count-1)
			for(uint32_t k=i-MAX_THREADS+1;k<=i;k++) pthread_join(threads[k%MAX_THREADS],NULL);
	}
	char cmd[4096]; snprintf(cmd,sizeof(cmd),"ar rcs %s/%s.a",build_folder,lib->name);
	for(uint32_t i=0;i<lib->file_count;i++){
		char* obj=change_c_to_o(lib->files[i]);
		strcat(cmd," "); strcat(cmd,build_folder); strcat(cmd,"/"); strcat(cmd,obj);
		free(obj);
	}
	printf(BOLD COLOR_GREEN "Creating static library: %s/%s.a\n" COLOR_RESET,build_folder,lib->name);
	if(system(cmd)!=0){ fprintf(stderr,BOLD COLOR_RED "Library creation failed\n" COLOR_RESET); exit(-1);}
}

int main(int argc,char* argv[]){
	printf(BOLD COLOR_YELLOW "Welcome to Diddy Builder Party!\n" COLOR_RESET);
	ArgumentsEnabled args=process_args(argc,argv);
	if(args.clean){ char cmd[512]; snprintf(cmd,sizeof(cmd),"rm -rf %s",args.build_folder); system(cmd); printf(BOLD COLOR_YELLOW "Cleaned build folder %s\n" COLOR_RESET,args.build_folder); exit(0);}
	if(args.debug) printf(BOLD COLOR_YELLOW "Debug build\n" COLOR_RESET); else printf(BOLD COLOR_YELLOW "Release build\n" COLOR_RESET);

	ParseDiddy("diddy");
	mkdir_if_not_exists(args.build_folder);

	printf(BOLD COLOR_YELLOW "COMPILING LIBRARIES...\n" COLOR_RESET);
	for(uint32_t i=0;i<dd_libs.count;i++) compile_library(&dd_libs.libs[i],args.build_folder,args.verbose);

	printf(BOLD COLOR_YELLOW "COMPILING EXECUTABLES...\n" COLOR_RESET);
	for(uint32_t i=0;i<dd_exes.count;i++){
		DiddyExecutable* exe=&dd_exes.exes[i];
		pthread_t threads[MAX_FILE_PER_PROJECT];
		CompileTask tasks[MAX_FILE_PER_PROJECT];
		for(uint32_t j=0;j<exe->file_count;j++){
			tasks[j].src=exe->files[j]; tasks[j].build_folder=args.build_folder; tasks[j].verbose=args.verbose;
			pthread_create(&threads[j%MAX_THREADS],NULL,compile_file,&tasks[j]);
			if(j%MAX_THREADS==MAX_THREADS-1||j==exe->file_count-1)
				for(uint32_t k=j-MAX_THREADS+1;k<=j;k++) pthread_join(threads[k%MAX_THREADS],NULL);
		}
		char cmd[4096]; snprintf(cmd,sizeof(cmd),"gcc -o %s/%s",args.build_folder,args.output?args.output:exe->name);
		for(uint32_t j=0;j<exe->file_count;j++){
			char* obj=change_c_to_o(exe->files[j]);
			strcat(cmd," "); strcat(cmd,args.build_folder); strcat(cmd,"/"); strcat(cmd,obj);
			free(obj);
		}
		for(uint32_t l=0;l<dd_libs.count;l++){
			strcat(cmd," "); strcat(cmd,args.build_folder); strcat(cmd,"/"); strcat(cmd,dd_libs.libs[l].name); strcat(cmd,".a");
		}
		printf(BOLD COLOR_YELLOW "Linking executable: %s\n" COLOR_RESET,exe->name);
		if(system(cmd)!=0){ fprintf(stderr,BOLD COLOR_RED "Linking failed for %s\n" COLOR_RESET,exe->name); exit(-1);}
		if(args.install_path){
			char install_cmd[512]; snprintf(install_cmd,sizeof(install_cmd),"cp %s/%s %s/",args.build_folder,args.output?args.output:exe->name,args.install_path);
			if(system(install_cmd)!=0){ fprintf(stderr,BOLD COLOR_RED "Install failed for %s\n" COLOR_RESET,exe->name); exit(-1);}
			printf(BOLD COLOR_GREEN "Installed %s to %s\n" COLOR_RESET,exe->name,args.install_path);
		}
	}

	printf(BOLD COLOR_GREEN "BUILD COMPLETE!\n" COLOR_RESET);
	return 0;
}
