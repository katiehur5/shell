#include "process.h"
#include "parse.h"

int handle_simple (const CMD *cmd);
int handle_pipe (const CMD *cmd);
int handle_and (const CMD *cmd);
int handle_or (const CMD *cmd);
int handle_subcmd (const CMD *cmd);
//int handle_end (const CMD *cmd, bool ignore_right);
int handle_bg_end (const CMD *cmd, bool ignore_right);
int redirect (const CMD *cmd);
int define (const CMD *cmd);

void reap_zombies();

// struct for directory stack
typedef struct dir {
    char *path;
    struct dir *next;
} dir;

static dir *dir_stack_head = NULL;
int print_dir_stack();

int handle_cd(const CMD *cmd, bool print);
int handle_pushd(const CMD *cmd, bool print);
int handle_popd(const CMD *cmd, bool print);

void reap_zombies () {
    int status = 0;
    pid_t wpid = 1;
    while (wpid > 0) {
        wpid = waitpid(-1, &status, WNOHANG);
        if (wpid > 0) {
            fprintf(stderr, "Completed: %d (%d)\n", wpid, STATUS(status));
        }
    }
}

int process (const CMD *cmdList) {
    //printf("IN %s\n", getcwd(NULL,0));
    reap_zombies();
    int status;
    if (cmdList != NULL) {
        switch (cmdList->type) {
            case SIMPLE:
                status = handle_simple(cmdList);
                break;
            
            case PIPE:
                status = handle_pipe(cmdList);
                break;

            case SEP_AND:
                status = handle_and(cmdList);
                break;

            case SEP_OR:
                status = handle_or(cmdList);
                break;

            case SUBCMD:
                status = handle_subcmd(cmdList);
                break;
            
            case SEP_BG:
                status = handle_bg_end(cmdList, false);
                break;

            case SEP_END:
                status = handle_bg_end(cmdList, false);
                break;

        }
    }
/*
    dir *curr = dir_stack_head;
    while (curr) {
        dir *temp = curr->next;
        free(curr->path);
        free(curr);
        curr = temp;
    }*/

    int new_val = status;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", new_val);
    //printf("%s\n", buffer);
    setenv("?", buffer, 1);
    return new_val;
}

int handle_bg_end(const CMD *cmd, bool ignore_right) {
    //int status;
    int temp_errno;

    if (cmd->left->type == SEP_END || cmd->left->type == SEP_BG) {
        handle_bg_end(cmd->left, true);
        // I'm a &
        if (cmd->type == SEP_BG) {
            pid_t pid = fork();
            if (pid < 0) {
                temp_errno = errno;
                perror("fork");
                return temp_errno;
            }
            if (pid == 0) {
                exit(process(cmd->left->right));
            }
            else {
                fprintf(stderr, "Backgrounded: %d\n", pid);
                if (!ignore_right) {
                    if (cmd->right) {
                        return process(cmd->right);
                    }
                }
            }

        }

        // I'm a ;
        else if (cmd->type == SEP_END) {
            process(cmd->left->right);
            if (!ignore_right) {
                if (cmd->right) {
                    return process(cmd->right);
                }
            }
        }
    }

    else {
        //if bg
            //fork for left side
            // process right side if not ignore_right
        //if end
            // process left side
            // process right side
        if (cmd->type == SEP_BG) {
            pid_t pid = fork();
            if (pid < 0) {
                temp_errno = errno;
                perror("fork");
                return temp_errno;
            }
            if (pid == 0) {
                exit(process(cmd->left));
            }
            else {
                fprintf(stderr, "Backgrounded: %d\n", pid);
                if (!ignore_right) {
                    if (cmd->right) {
                        return process(cmd->right);
                    }
                }
                return 0;
            }
        }

        else if (cmd->type == SEP_END) {
            process(cmd->left);
            if (!ignore_right) {
                if (cmd->right) {
                    return process(cmd->right);
                }
            }
        }
    }
    return 0;
}

int handle_subcmd (const CMD *cmd) {
    int status;
    int temp_errno;

    pid_t pid = fork();

    if (pid < 0) {
        temp_errno = errno;
        perror("fork");
        return temp_errno;
    }

    if (pid == 0) {
        if ((status = define(cmd))!=0) {
            exit(status);
        }
        if ((status = redirect(cmd))!=0) {
            exit(status);
        }
        exit(process(cmd->left));
    }
    else {
        waitpid(pid, &status, 0);

        return STATUS(status);
    }
}

int handle_and (const CMD *cmd) {
    int status;

    if ((status = process(cmd->left)) == 0) {
        return process(cmd->right);
    }
    else {
        return status;
    }
}

int handle_or (const CMD *cmd) {
    int status;

    if ((status = process(cmd->left)) != 0) {
        return process(cmd->right);
    }
    else {
        return status;
    }
}

int handle_pipe (const CMD *cmd) {
    int fd[2];
    int temp_errno;
    // int status;

    if (pipe(fd) < 0) {
        temp_errno = errno;
        perror("pipe");
        return temp_errno;
    }

    pid_t left_pid = fork();
    if (left_pid < 0) {
        temp_errno = errno;
        perror("fokr");
        close(fd[0]);
        close(fd[1]);
        return temp_errno;
    }

    if (left_pid == 0) {
        dup2(fd[1],1);
        close(fd[0]);
        close(fd[1]);

        exit(process(cmd->left));
    }

    pid_t right_pid = fork();
    if (right_pid < 0) {
        temp_errno = errno;
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return temp_errno;
    }

    if (right_pid == 0) {
        dup2(fd[0],0);
        close(fd[0]);
        close(fd[1]);

        exit(process(cmd->right));
    }

    close(fd[0]);
    close(fd[1]);

    int left_status;
    int right_status;

    waitpid(left_pid, &left_status, 0);
    waitpid(right_pid, &right_status, 0);

    if (STATUS(right_status) != 0) {
        return STATUS(right_status);
    }
    else {
        return STATUS(left_status);
    }
}
int handle_cd(const CMD *cmd, bool print) {
    int temp_errno;
    char *path;
    // no arguments -> change directory to $HOME
    if (cmd->argc == 1) {
        path = getenv("HOME");
    }
    else if (cmd->argc == 2) {
        path = cmd->argv[1];
    }
    else {
        if (print) {
        fprintf(stderr, "usage: cd OR cd <dirName>\n");
        }
        return 1;
    }

    if (chdir(path) < 0) {
        if (print) {
        temp_errno = errno;
        perror("chdir");
        return temp_errno;
        }
    }

    return 0;
}

int handle_pushd(const CMD *cmd, bool print) {
    int temp_errno;
    int status;
    if (cmd->argc != 2) {
        if (print) {
        fprintf(stderr, "usage: pushd: <dirName>\n");
        }
        return 1;
    }

    char *cwd = getcwd(NULL, 0);
    //printf("in %s\n", cwd);
    if (!cwd) {
        if (print) {
        temp_errno = errno;
        perror("getcwd");
        return temp_errno;
        }
    }
    if (chdir(cmd->argv[1]) != 0) {
        if (print) {
        temp_errno = errno;
        perror("chdir");
        free(cwd);
        return temp_errno;
        }
    }
    // printf("in %s\n", getcwd(NULL,0));
    dir *node = malloc(sizeof(dir));
    if (node == NULL) {
        return 1;
    }
    node->path = strdup(cwd);
    if (node->path == NULL) {
        temp_errno = errno;
        perror("strdup");
        free(cwd);
        return temp_errno;
    }
    free(cwd);
    node->next = dir_stack_head;
    dir_stack_head = node;
    if(print) {
        if ((status = print_dir_stack()) < 0) {
            return status;
        }
    }

    return 0;
}

int handle_popd(const CMD *cmd, bool print) {
    int temp_errno;
    int status;
    if (cmd->argc != 1) {
        if (print) {
        fprintf(stderr, "usage: popd\n");
        }
        return 1;
    }

    if (dir_stack_head == NULL) {
        if (print) {
        fprintf(stderr, "dir stack empty\n");
        }
        return 1;
    }

    dir *popped = dir_stack_head;
    dir_stack_head = dir_stack_head->next;

    char *path = popped->path;
    free(popped);
    
    if (chdir(path) != 0) {
        if (print) {
        temp_errno = errno;
        perror("chdir");
        free(path);
        return temp_errno;
        }
    }
    if (print) {
        if ((status = print_dir_stack()) < 0) {
            return status;
        }
    }
    free(path);

    return 0;
}

int handle_simple (const CMD *cmd) {
    int temp_errno;
    int status;

    // fork process
    pid_t pid = fork();

    if (pid < 0) {
        temp_errno = errno;
        perror("fork");
        return temp_errno;
    }

    if (pid == 0) {
        // handle definitions
        if ((status = define(cmd))!=0) {
            exit(status);
        }
        if ((status = redirect(cmd))!=0) {
            exit(status);
        }

        // handle cd
        if (strcmp(cmd->argv[0], "cd") == 0) {
            exit(handle_cd(cmd, true));
        }
        // handle pushd
        else if (strcmp(cmd->argv[0], "pushd") == 0) {
            exit(handle_pushd(cmd, true));
        }

        // handle popd
        else if (strcmp(cmd->argv[0], "popd") == 0) {
            exit(handle_popd(cmd, true));
        }


        else {
            if (execvp(cmd->argv[0], cmd->argv) != 0) {
                temp_errno = errno;
                perror("execvp");
                exit(temp_errno);
            }
        }
    }
    else if(pid > 0) {
        // handle cd
        if (strcmp(cmd->argv[0], "cd") == 0) {
            //return(handle_cd(cmd, false));
            status = handle_cd(cmd, false);
        }
        // handle pushd
        else if (strcmp(cmd->argv[0], "pushd") == 0) {
            //return(handle_pushd(cmd, false));
            status = handle_pushd(cmd, false);
        }

        // handle popd
        else if (strcmp(cmd->argv[0], "popd") == 0) {
            //return(handle_popd(cmd, false));
            status = handle_popd(cmd, false);
        }
        waitpid(pid, &status, 0);
        
        int new_val = STATUS(status);
        //printf("%d\n", new_val);
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%d", new_val);
        setenv("?", buffer, 1);
        return new_val;
    }
    return 0;
}
/*
int handle_simple (const CMD *cmd) {
    int temp_errno;
    int status;
    
    // handle cd
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if ((status = define(cmd))!=0) {
            exit(status);
        }
        if ((status = redirect(cmd))!=0) {
            exit(status);
        }
        char *path;
        // no arguments -> change directory to $HOME
        if (cmd->argc == 1) {
            path = getenv("HOME");
        }
        else if (cmd->argc == 2) {
            path = cmd->argv[1];
        }
        else {
            fprintf(stderr, "usage: cd OR cd <dirName>\n");
            return 1;
        }

        if (chdir(path) < 0) {
            temp_errno = errno;
            perror("chdir");
            return temp_errno;
        }

        return 0;
    }
    // handle pushd
    else if (strcmp(cmd->argv[0], "pushd") == 0) {
        if ((status = define(cmd))!=0) {
            exit(status);
        }
        if ((status = redirect(cmd))!=0) {
            exit(status);
        }

        if (cmd->argc != 2) {
            fprintf(stderr, "usage: pushd: <dirName>\n");
            return 1;
        }

        char *cwd = getcwd(NULL, 0);
        //printf("in %s\n", cwd);
        if (!cwd) {
            temp_errno = errno;
            perror("getcwd");
            return temp_errno;
        }
        if (chdir(cmd->argv[1]) != 0) {
            temp_errno = errno;
            perror("chdir");
            free(cwd);
            return temp_errno;
        }
        // printf("in %s\n", getcwd(NULL,0));
        dir *node = malloc(sizeof(dir));
        if (node == NULL) {
            return 1;
        }
        node->path = strdup(cwd);
        if (node->path == NULL) {
            temp_errno = errno;
            perror("strdup");
            free(cwd);
            return temp_errno;
        }
        free(cwd);
        node->next = dir_stack_head;
        dir_stack_head = node;
        if ((status = print_dir_stack()) < 0) {
            return status;
        }

        return 0;
    }

    // handle popd
    else if (strcmp(cmd->argv[0], "popd") == 0) {
        if ((status = define(cmd))!=0) {
            exit(status);
        }
        if ((status = redirect(cmd))!=0) {
            exit(status);
        }
        if (cmd->argc != 1) {
            fprintf(stderr, "usage: popd\n");
            return 1;
        }

        if (dir_stack_head == NULL) {
            fprintf(stderr, "dir stack empty\n");
            return 1;
        }

        dir *popped = dir_stack_head;
        dir_stack_head = dir_stack_head->next;

        char *path = popped->path;
        free(popped);
        
        if (chdir(path) != 0) {
            temp_errno = errno;
            perror("chdir");
            free(path);
            return temp_errno;
        }
        if ((status = print_dir_stack()) < 0) {
            return status;
        }
        free(path);

        return 0;
    }
    // fork process
    pid_t pid = fork();

    if (pid < 0) {
        temp_errno = errno;
        perror("fork");
        return temp_errno;
    }

    if (pid == 0) {
        // handle definitions
        if ((status = define(cmd))!=0) {
            exit(status);
        }
        if ((status = redirect(cmd))!=0) {
            exit(status);
        }


        // handle cd
        if (strcmp(cmd->argv[0], "cd") == 0) {
            char *path;
            // no arguments -> change directory to $HOME
            if (cmd->argc == 1) {
                path = getenv("HOME");
            }
            else if (cmd->argc == 2) {
                path = cmd->argv[1];
            }
            else {
                fprintf(stderr, "usage: cd OR cd <dirName>\n");
                return 1;
            }

            if (chdir(path) < 0) {
                temp_errno = errno;
                perror("chdir");
                return temp_errno;
            }

            return 0;
        }
        // handle pushd
        else if (strcmp(cmd->argv[0], "pushd") == 0) {
            if (cmd->argc != 2) {
                fprintf(stderr, "usage: pushd: <dirName>\n");
                return 1;
            }

            char *cwd = getcwd(NULL, 0);
            //printf("in %s\n", cwd);
            if (!cwd) {
                temp_errno = errno;
                perror("getcwd");
                return temp_errno;
            }
            if (chdir(cmd->argv[1]) != 0) {
                printf("here\n");
                temp_errno = errno;
                perror("chdir");
                free(cwd);
                return temp_errno;
            }
            // printf("in %s\n", getcwd(NULL,0));
            dir *node = malloc(sizeof(dir));
            if (node == NULL) {
                return 1;
            }
            node->path = strdup(cwd);
            if (node->path == NULL) {
                temp_errno = errno;
                perror("strdup");
                free(cwd);
                return temp_errno;
            }
            node->next = dir_stack_head;
            dir_stack_head = node;
            if ((status = print_dir_stack()) < 0) {
                return status;
            }

            return 0;
        }

        // handle popd
        else if (strcmp(cmd->argv[0], "popd") == 0) {
            if (cmd->argc != 1) {
                fprintf(stderr, "usage: popd\n");
                return 1;
            }

            if (dir_stack_head == NULL) {
                fprintf(stderr, "dir stack empty\n");
                return 1;
            }

            dir *popped = dir_stack_head;
            dir_stack_head = dir_stack_head->next;

            char *path = popped->path;
            free(popped);
            
            if (chdir(path) != 0) {
                temp_errno = errno;
                perror("chdir");
                free(path);
                return temp_errno;
            }
            if ((status = print_dir_stack()) < 0) {
                return status;
            }
            free(path);

            return 0;
        }

        //else {
            if (execvp(cmd->argv[0], cmd->argv) != 0) {
                temp_errno = errno;
                perror("execvp");
                exit(temp_errno);
            }
        //}
    }
    else if(pid > 0) {
        waitpid(pid, &status, 0);
        
        int new_val = STATUS(status);
        //printf("%d\n", new_val);
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%d", new_val);
        setenv("?", buffer, 1);
        return new_val;
    }
    return 0;
}
*/
int print_dir_stack() {
    int temp_errno;
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        temp_errno = errno;
        perror("getcwd");
        return temp_errno;
    }

    fprintf(stdout, "%s",cwd);
    dir *curr = dir_stack_head;
    while (curr != NULL) {
        fprintf(stdout, " %s", curr->path);
        curr = curr->next;
    }
    fprintf(stdout, "\n");
    free(cwd);
    return 0;
}

int redirect (const CMD *cmd) {
    // use dup2
    int new;
    int temp_errno;

    // stdin <
    if (cmd->fromType == RED_IN) {
        if ((new = open(cmd->fromFile, O_RDONLY, 0644)) < 0) {
            temp_errno = errno;
            perror("open");
            return temp_errno;
        }
        else {
            dup2(new, 0);
            close(new);
        }
    }
    // stdin << (HEREDOC)
    else if (cmd->fromType == RED_IN_HERE) {
        char fileName[] = "/tmp/pathXXXXXX";
        int fd = mkstemp(fileName);

        if (fd < 0) {
            temp_errno = errno;
            perror("mkstemp");
            return temp_errno;
        }
        
        if (write(fd, cmd->fromFile, strlen(cmd->fromFile)) < 0) {
            temp_errno = errno;
            close(fd);
            unlink(fileName);
            perror("write");
            return temp_errno;
        }

        if (lseek(fd, 0, SEEK_SET) == -1) {
            temp_errno = errno;
            close(fd);
            unlink(fileName);
            perror("lseek");
            return temp_errno;
        }

        unlink(fileName);
        dup2(fd, 0);
        close(fd);
    }

    // stdout >
    if (cmd->toType == RED_OUT) {
        if ((new = open(cmd->toFile, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
            temp_errno = errno;
            perror("open");
            return temp_errno;
        }
        else {
            dup2(new, 1);
            close(new);
        }
    }

    // stdout >>
    else if (cmd->toType == RED_OUT_APP) {
        if ((new = open(cmd->toFile, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0) {
            temp_errno = errno;
            perror("open");
            return temp_errno;
        }
        else {
            dup2(new, 1);
            close(new);
        }
    }

    return 0;
}

int define(const CMD *cmd) {
    int temp_errno;
    // assign local variables
    for (int i = 0; i < cmd->nLocal; i++) {
        if (setenv(cmd->locVar[i], cmd->locVal[i], 1) < 0) {
            temp_errno = errno;
            perror("setenv");
            return temp_errno;
        }
    }
    return 0;
}