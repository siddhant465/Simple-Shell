#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <glob.h>

#define SH_TOK_BUFSIZE 64
#define SH_TOK_DELIM " \t\r\n\a"

extern char **environ;

char **HISTORY;
char *INPUT_FILE, *OUTPUT_FILE;
unsigned int INPUT_REDIRECTION = 0, OUTPUT_REDIRECTION = 0, BACKGROUND = 0, GLOB = 0, MULTI = 0;

void getHistory();
void storeHistory(char *line);

char **sh_split_line(char *line)
{
    int bufsize = SH_TOK_BUFSIZE;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens)
    {
        fprintf(stderr, "sh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, SH_TOK_DELIM);
    while (token != NULL)
    {
        tokens[position] = token;
        position++;

        if (position >= bufsize)
        {
            bufsize += SH_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens)
            {
                free(tokens_backup);
                fprintf(stderr, "sh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, SH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

char *sh_read_line(void)
{
    char *line = NULL;
    size_t bufsize = 0; // have getline allocate a buffer for us

    if (getline(&line, &bufsize, stdin) == -1)
    {
        if (feof(stdin)) // EOF
        {
            fprintf(stderr, "EOF\n");
            exit(EXIT_SUCCESS);
        }
        else
        {
            fprintf(stderr, "Value of errno: %d\n", errno);
            exit(EXIT_FAILURE);
        }
    }

    storeHistory(line);
    return line;
}

void resetGlobals()
{
    INPUT_REDIRECTION = 0;
    OUTPUT_REDIRECTION = 0;
    BACKGROUND = 0;
    GLOB = 0;
}

void setInputRedirect(char *FILE)
{
    int fd;
    fd = open(FILE, O_RDONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error opening file. \n");
        exit(-1);
    }
    dup2(fd, STDIN_FILENO);
    close(fd);
}

void setOutputRedirect(char *FILE)
{
    int fd;
    fd = open(FILE, O_CREAT | O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error opening file. \n");
        exit(-1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

int foo(char const *epath, int eerrno)
{
    return 0;
}

void setGlob(char **args)
{
    int ret;
    char *list = NULL;
    char *final_list = NULL;
    char **final_args = NULL;
    list = malloc(sizeof(char) * SH_TOK_BUFSIZE);
    final_list = malloc(sizeof(char) * SH_TOK_BUFSIZE);
    glob_t results = {0};

    glob(args[GLOB], GLOB_DOOFFS, foo, &results);

    for (size_t i = 0; i != results.gl_pathc; ++i)
    {
        strcat(list, results.gl_pathv[i]);
        strcat(list, " ");
    }

    strcat(final_list, args[0]);
    strcat(final_list, " ");
    strcat(final_list, list);

    // printf("FINAL LIST!!! %s\n", final_list);

    final_args = sh_split_line(final_list);

    if (execvpe(args[0], final_args, environ) == -1)
    {
        fprintf(stderr, "Error executing command.\n");
        exit(EXIT_FAILURE);
    }

    globfree(&results);
    free(list);
    free(final_list);
    free(final_args);
    free(args);
}

int sh_launch(char **args)
{
    if (INPUT_REDIRECTION)
        setInputRedirect(INPUT_FILE);

    if (OUTPUT_REDIRECTION)
        setOutputRedirect(OUTPUT_FILE);

    if (strcmp(args[0], "cd") == 0) //CD
    {
        chdir(args[1]);
        return 1;
    }

    if (strcmp(args[0], "history") == 0)
    {
        getHistory();
        return 1;
    }

    if (GLOB)
    {
        setGlob(args);
        return 1;
    }

    if (execvpe(args[0], args, environ) == -1)
    {
        fprintf(stderr, "Error executing command.\n");
        exit(EXIT_FAILURE);
    }

    return 1;
}

void parseArgs(char **args)
{
    char *globfinder;

    for (int i = 0; args[i] != NULL; i++)
    {
        switch (args[i][0])
        {
        case '<':
            INPUT_REDIRECTION++;
            INPUT_FILE = args[i + 1];
            args[i] = '\0';
            break;

        case '>':
            OUTPUT_REDIRECTION++;
            OUTPUT_FILE = args[i + 1];
            args[i] = '\0';
            break;

        case '&':
            BACKGROUND++;
            args[i] = '\0';
            break;

        case ';':
            MULTI++;
            break;

        default:
            break;
        }
    }

    for (int i = 0; args[i] != NULL; i++)
    {
        for (int j = 0; args[i][j] != '\0'; j++)
        {
            if (args[i][j] == '*' || args[i][j] == '?')
                GLOB = i;
        }
    }
}

int multi_commands(char **args)
{
    int semicolon_pos = -1;

    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], ";") == 0)
        {
            semicolon_pos = i;
            break;
        }
    }

    if (semicolon_pos != -1)
    {
        char **left_pipe = &args[0];
        left_pipe[semicolon_pos] = '\0';

        char **right_pipe = &args[semicolon_pos + 1];

        if (fork() == 0)
        {
            sh_launch(left_pipe);
        }

        if (fork() == 0)
        {
            multi_commands(right_pipe);
        }

        wait(NULL);
        wait(NULL);
        return 1;
    }
    else
    {

        sh_launch(args);
    }

    return 1;
}

int sh_pipe(char **args)
{
    int pipe_pos = -1;

    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "|") == 0)
        {
            pipe_pos = i;
            break;
        }
    }

    if (pipe_pos != -1)
    {
        char **left_pipe = &args[0];
        left_pipe[pipe_pos] = '\0';

        char **right_pipe = &args[pipe_pos + 1];

        int pipefd[2];

        if (pipe(pipefd) == -1)
        {
            fprintf(stderr, "Error creating pipe.\n");
            exit(EXIT_FAILURE);
        }

        if (fork() == 0)
        {
            close(1);
            dup(pipefd[1]);
            close(pipefd[0]);
            close(pipefd[1]);
            sh_launch(left_pipe);
        }

        if (fork() == 0)
        {
            close(0);
            dup(pipefd[0]);
            close(pipefd[0]);
            close(pipefd[1]);
            sh_pipe(right_pipe);
        }

        close(pipefd[0]);
        close(pipefd[1]);
        wait(NULL);
        wait(NULL);
        return 1;
    }
    else
    {

        sh_launch(args);
    }

    return 1;
}

int sh_execute(char **args)
{
    if (args[0] == NULL)
    {
        return 1; // An empty command was entered.
    }

    pid_t pid;
    pid = fork();

    parseArgs(args);

    if (pid == 0)
    {
        if (MULTI)
            return multi_commands(args);
        else
            return sh_pipe(args);
    }
    else if (pid < 0)
    {
        fprintf(stderr, "Error creating child.\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        if (BACKGROUND == 0)
            wait(NULL);
    }

    return 1;
}

void getHistory()
{
    int i = 0;
    FILE *hist = fopen("hist.txt", "r");
    char *line = NULL;

    if (hist == NULL)
    {
        fprintf(stderr, "Error opening history file.");
        exit(EXIT_FAILURE);
    }

    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, hist)) != -1)
    {
        i++;
        printf("%d %s", i, line);
    }

    fclose(hist);

    if (line)
        free(line);
}

void storeHistory(char *line)
{
    FILE *hist;
    hist = fopen("hist.txt", "a");
    if (hist == NULL)
    {
        fprintf(stderr, "Error opening history file.\n");
    }
    fwrite(line, sizeof(char), strlen(line), hist);
    fclose(hist);
}

void sh_loop(void)
{
    char *line;
    char **args;
    int status;

    do
    {
        printf("my_shell$ ");
        line = sh_read_line();
        args = sh_split_line(line);
        status = sh_execute(args);

        free(line);
        free(args);
        resetGlobals();
    } while (status);
}

int main(int argc, char **argv)
{
    remove("hist.txt");
    sh_loop();
    return EXIT_SUCCESS;
}