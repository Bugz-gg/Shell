#define BUFFER_SIZE 255
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>


// pid_t children[100]; // Variable globale pour le handler.
// int nb_children = 0;

void exit_if(int condition, char *message)
{
    if (condition)
    {
        perror(message);
        exit(EXIT_FAILURE);
    }
}

int parse_line(char *s, char ***argv)
{ // Ne pas oublier de free argv et les char* de argv.
    int i = 0, mal = 0;
    char *temp = malloc(sizeof(char) * BUFFER_SIZE);
    *argv = malloc(sizeof(char *) * BUFFER_SIZE);
    while (s != NULL)
    {
        for (; isspace(*s); s++);
        mal = strcspn(s, " "); // La fonction strcspn() calcule la longueur du segment initial de s qui ne contient que des octets absents de reject.
        temp = realloc(temp, sizeof(char) * (mal + 1));
        for (int j = 0; j < mal; j++)
        {
            temp[j] = s[j];
        }
        temp[mal] = '\0';
        (*argv)[i] = strdup(temp); // Effectue une copie de la chaîne pointée par temp dans une nouvelle chaîne allouée dynamiquement.
        s = strpbrk(s, " ");
        i++;
    }
    (*argv)[i] = NULL;
    free(temp);
    return i;
}

void free_argv(char **argv, int argc)
{
    if (argv == NULL)
    {
        return;
    }
    for (int i = 0; i < argc; i++)
    {
        free(argv[i]);
    }
    free(argv);
}

void fn_pipe(int pipefd[2])
{ 
    char **result;
    fprintf(stderr, "pipe> ");
    size_t rd;
    char buffer[BUFFER_SIZE];
    rd = read(STDIN_FILENO, buffer, BUFFER_SIZE);
    exit_if(rd < 0, "Invalid Read.");
    buffer[rd] = '\0';
    close(pipefd[1]);
    pid_t pid = fork();
    switch (pid){
        case -1:
            perror("Erreur lors de la création du processus pour wc");
            exit(EXIT_FAILURE);
            break;
        case 0:
            strcpy(buffer, strtok(buffer, "\n"));
            if (strcmp(buffer, "exit") == 0)
            {
                exit(EXIT_SUCCESS);
                break;
            }
            parse_line(buffer, &result);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]); 
            execvp(result[0], result);
            perror("Erreur lors de l'exécution du pipe.");
            exit(EXIT_FAILURE);
            break;
        default:
            wait(NULL);
            close(pipefd[0]);
            if (strcmp(buffer, "exit") == 0)
            {
                exit(EXIT_SUCCESS);
                break;
            }
            break;
    }
}

// void myhandler(int sig){
//     fprintf(stderr, "J'ai reçu le signal %d\n", sig);
//     for (int i = 0; i < nb_children; i++){
//         kill(children[i], SIGKILL);
//     }
//     exit(EXIT_SUCCESS);
// }

int main()
{
    char **result;
    int pipefd[2] = {-1, -1};
    char buffer[BUFFER_SIZE];
    int argc;
    ssize_t rd;
    pid_t pid = getpid();
    fprintf(stderr, "My pid is %d\n", pid);
    sigset_t set, set_old;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    exit_if(sigprocmask(SIG_BLOCK, &set, &set_old) < 0, "Invalid sigprocmask.");
    struct sigaction sa, sa_old;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    exit_if(sigaction(SIGINT, &sa, &sa_old) < 0, "Invalid sigaction.");
    while (1)
    {
        fprintf(stderr, "$ ");
        rd = read(STDIN_FILENO, buffer, BUFFER_SIZE);
        exit_if(rd < 0, "Invalid Read.");
        if (rd > 2 && buffer[rd - 2] == '|')
        {
            pipe(pipefd);
            exit_if(pipefd[0] < 0 || pipefd[1] < 0, "Invalid Pipe");
        }
        buffer[rd] = '\0';
        pid_t pid = fork();
        exit_if(pid < 0, "Invalid Fork.");

        // On peut faire du refractoring ici en créant une fonction qui va gérer l'execution de la commande.
        switch (pid)
        {
        case 0: // Child + execution avec exec de la commande.
            exit_if(sigprocmask(SIG_UNBLOCK, &set, NULL) < 0, "Invalid sigprocmask.");    
            exit_if(sigaction(SIGINT, &sa_old, NULL) < 0, "Failed to unset custom SIGINT handler in child process.");     
            // children[nb_children] = getpid();
            // nb_children++;
            strcpy(buffer, strtok(buffer, "\n"));
            argc = parse_line(buffer, &result);
            if (argc > 2 && strcmp(result[argc - 2], ">") == 0)
            {
                // fprintf(stderr, "Redirection de la sortie standard vers le fichier %s\n", result[argc - 1]);
                int fd = open(result[argc - 1], O_RDWR | O_CREAT | O_TRUNC, 0644);
                exit_if(fd < 0, "Invalid Open.");
                dup2(fd, STDOUT_FILENO); // Redirection de la sortie standard vers le fichier ce qui permet de fermer le fichier après l'execution de la commande.
                close(fd);               // On n'a plus besoin du fichier après la redirection.
                free(result[argc - 1]);
                result[argc - 2] = NULL;
            }
            else if (pipefd[0] > 0 || pipefd[1] > 0)
            {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                result[argc - 1] = NULL;
                execvp(result[0], result);
            }
            execvp(result[0], result);
            if (strcmp(buffer, "exit") == 0)
            {
                free_argv(result, argc);
                exit(EXIT_SUCCESS);
                break;
            }
            perror("Erreur d'execution.");
            exit(EXIT_FAILURE);
            break;
        case -1:
            perror("Erreur de fork.");
            exit(EXIT_FAILURE);
            break;
        default: // Parent + attente de la fin de l'execution de la commande.
            wait(NULL);
            if (pipefd[0] > 0 || pipefd[1] > 0)
            {  
                fn_pipe(pipefd);
                pipefd[0] = -1;
                pipefd[1] = -1;
            }
            buffer[rd - 1] = '\0';
            if (strcmp(buffer, "exit") == 0)
            {
                exit(EXIT_SUCCESS);
                break;
            }
            exit_if(sigprocmask(SIG_BLOCK, &set, &set_old) < 0, "Invalid sigprocmask.");
            break;
        }
    }
    // sigaction(SIGINT, &sa_old, NULL);
    free_argv(result, argc);
    return EXIT_SUCCESS;
}