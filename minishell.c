#include <stdio.h>
#include <stdlib.h>
#include <readcmd.h>
#include <stdbool.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

void traitement_SIGCHLD(int sig) {
    (void)sig;

    int status;
    pid_t pid_fils;

    while ((pid_fils = waitpid(-1, &status,
                               WNOHANG | WUNTRACED | WCONTINUED)) > 0) {

        if (WIFEXITED(status)) {

            printf("\n[Processus %d terminé normalement (code %d)]\n",
                   pid_fils,
                   WEXITSTATUS(status));

        } else if (WIFSIGNALED(status)) {

            printf("\n[Processus %d tué par le signal %d]\n",
                   pid_fils,
                   WTERMSIG(status));

        } else if (WIFSTOPPED(status)) {

            printf("\n[Processus %d suspendu (signal %d)]\n",
                   pid_fils,
                   WSTOPSIG(status));

        } else if (WIFCONTINUED(status)) {

            printf("\n[Processus %d repris]\n",
                   pid_fils);
        }
    }
}

void cmd_cd(char *chemin) {

    if (chemin == NULL) {

        chemin = getenv("HOME");

        if (chemin == NULL) {

            fprintf(stderr, "cd: HOME non défini\n");
            return;
        }
    }

    if (chdir(chemin) == -1) {

        perror("cd");
    }
}

void cmd_rep(char *repertoire) {

    if (repertoire == NULL) {
        repertoire = ".";
    }

    DIR *dir = opendir(repertoire);

    if (dir == NULL) {

        perror("rep");
        return;
    }

    struct dirent *entree;

    while ((entree = readdir(dir)) != NULL) {

        printf("%s\n", entree->d_name);
    }

    closedir(dir);
}

static void appliquer_redirections(cmdline cmd) {

    /* redirection entrée */

    if (cmd->in != NULL) {

        int fd_in = open(cmd->in, O_RDONLY);

        if (fd_in == -1) {

            perror(cmd->in);
            exit(EXIT_FAILURE);
        }

        if (dup2(fd_in, STDIN_FILENO) == -1) {

            perror("dup2 stdin");
            exit(EXIT_FAILURE);
        }

        close(fd_in);
    }

    /* redirection sortie */

    if (cmd->out != NULL) {

        int fd_out = open(cmd->out,
                          O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR |
                          S_IRGRP | S_IROTH);

        if (fd_out == -1) {

            perror(cmd->out);
            exit(EXIT_FAILURE);
        }

        if (dup2(fd_out, STDOUT_FILENO) == -1) {

            perror("dup2 stdout");
            exit(EXIT_FAILURE);
        }

        close(fd_out);
    }
}

int main(void) {



    struct sigaction action;

    action.sa_handler = traitement_SIGCHLD;

    sigemptyset(&action.sa_mask);

    action.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &action, NULL) == -1) {

        perror("sigaction");
        exit(EXIT_FAILURE);
    }



    sigset_t masque_clavier;

    sigemptyset(&masque_clavier);

    sigaddset(&masque_clavier, SIGINT);
    sigaddset(&masque_clavier, SIGTSTP);

    if (sigprocmask(SIG_BLOCK,
                    &masque_clavier,
                    NULL) == -1) {

        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    bool fini = false;

    while (!fini) {

        char *ligne_commande = readline("> ");

        if (ligne_commande == NULL) {

            printf("Au revoir ...\n");
            break;
        }

        if (strlen(ligne_commande) > 0) {
            add_history(ligne_commande);
        }

        cmdline cmd;
        cmderror err;

        err = readcmd(ligne_commande, &cmd);

        free(ligne_commande);

        if (err != cmdNoError) {

            fprintf(stderr,
                    "Erreur lecture commande : %s\n",
                    cmd_errstring(err));

            continue;
        }

        if (cmd == NULL || cmd->fragments == NULL) {
            continue;
        }

        cmdfragment *les_commandes = cmd->fragments;



        if (les_commandes->next == NULL) {

            char **une_commande = les_commandes->command;

            if (une_commande == NULL ||
                une_commande[0] == NULL) {

                cmd_dispose(cmd);
                continue;
            }


            if (strcmp(une_commande[0], "exit") == 0) {

                fini = true;
                printf("Au revoir ...\n");
            }


            else if (strcmp(une_commande[0], "cd") == 0) {

                cmd_cd(une_commande[1]);
            }


            else if (strcmp(une_commande[0], "rep") == 0) {

                cmd_rep(une_commande[1]);
            }

            /* commande normale */

            else {

                pid_t pid = fork();

                if (pid == -1) {

                    perror("fork");
                }

                else if (pid == 0) {

                    /* fils */

                    if (cmd->backgrounded) {
                        setpgrp();
                    }

                    sigprocmask(SIG_UNBLOCK,
                                &masque_clavier,
                                NULL);

                    appliquer_redirections(cmd);

                    execvp(une_commande[0],
                           une_commande);

                    perror(une_commande[0]);

                    exit(EXIT_FAILURE);
                }

                else {

                    /* père */

                    if (!cmd->backgrounded) {

                        waitpid(pid, NULL, 0);

                    } else {

                        printf("[Tâche de fond lancée : PID %d]\n",
                               pid);
                    }
                }
            }
        }


        else {

            int ancien_tube[2] = {-1, -1};
            int nouveau_tube[2];

            cmdfragment *courant = cmd->fragments;

            while (courant != NULL) {

                /* créer tube sauf dernière commande */

                if (courant->next != NULL) {

                    if (pipe(nouveau_tube) == -1) {

                        perror("pipe");
                        exit(EXIT_FAILURE);
                    }
                }

                pid_t pid = fork();

                if (pid == -1) {

                    perror("fork");
                    exit(EXIT_FAILURE);
                }

                else if (pid == 0) {



                    sigprocmask(SIG_UNBLOCK,
                                &masque_clavier,
                                NULL);

                    /* entrée depuis ancien tube */

                    if (ancien_tube[0] != -1) {

                        dup2(ancien_tube[0],
                             STDIN_FILENO);
                    }

                    /* sortie vers nouveau tube */

                    if (courant->next != NULL) {

                        dup2(nouveau_tube[1],
                             STDOUT_FILENO);
                    }

                    /* fermer anciens tubes */

                    if (ancien_tube[0] != -1) {
                        close(ancien_tube[0]);
                    }

                    if (ancien_tube[1] != -1) {
                        close(ancien_tube[1]);
                    }

                    /* fermer nouveaux tubes */

                    if (courant->next != NULL) {

                        close(nouveau_tube[0]);
                        close(nouveau_tube[1]);
                    }

                    appliquer_redirections(cmd);

                    execvp(courant->command[0],
                           courant->command);

                    perror(courant->command[0]);

                    exit(EXIT_FAILURE);
                }



                if (ancien_tube[0] != -1) {
                    close(ancien_tube[0]);
                }

                if (ancien_tube[1] != -1) {
                    close(ancien_tube[1]);
                }

                ancien_tube[0] = nouveau_tube[0];
                ancien_tube[1] = nouveau_tube[1];

                if (courant->next != NULL) {
                    close(nouveau_tube[1]);
                }

                courant = courant->next;
            }

            /* attendre tous les fils */

            while (wait(NULL) > 0);
        }

        cmd_dispose(cmd);
    }

    return EXIT_SUCCESS;
}
