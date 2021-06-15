#include <dlfcn.h>
#include <inttypes.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#define EVENT_FILE_DEF "/run/motor_event"       // file for controlling the motor (by writing to it)
#define POSITION_FILE_DEF "/run/motor_position" // file to store the inicial motor position
#define STATUS_FILE_DEF "/run/motor_status"     // file for status of motor, to know if max offset of direction

#define PAN_REVERSE 0
#define PAN_FORWARD 1
#define TILT_FORWARD 2
#define TILT_REVERSE 3

#define H_MIN -260
#define H_CENTER 0 //Center pos horizontal
#define H_MAX 260 //Max steps horizontal

#define V_MIN-45
#define V_CENTER 0 //Center pos vertical
#define V_MAX 45 //Max steps vertical

int H_POSITION = 0;
int V_POSITION = 0;
char* EVENT_FILE = EVENT_FILE_DEF;
char* POSITION_FILE = POSITION_FILE_DEF;
char* STATUS_FILE = STATUS_FILE_DEF;

void (*motor_init)();
void (*motor_exit)();

void (*motor_h_dir_set)(int direction);
void (*motor_h_position_get)();
void (*motor_h_dist_set)(int steps);
void (*motor_h_move)();
void (*motor_h_stop)();

void (*motor_v_dir_set)(int direction);
void (*motor_v_position_get)();
void (*motor_v_dist_set)(int steps);
void (*motor_v_move)();
void (*motor_v_stop)();

void miio_motor_move(int direction, int steps)
{
    /* use xiaomi function from shared libary for 
    controlling the motor */
    printf("[DEBUG] raw_motor_move %d %d \n", direction, steps);
    motor_h_dir_set(direction);
    motor_h_position_get();
    motor_h_dist_set(steps);
    motor_h_move();
    motor_h_stop();
}

char **split(char string[], const char *sep)
{
    /*
    get string pointer and sep , return array of splited string 
    don't forget to free the return pointer 
    */
    char *token = strtok(string, sep);
    char **argv = calloc(1, sizeof(char *));

    int i = 0;
    while (token != NULL)
    {
        argv = realloc(argv, sizeof(argv) + sizeof(char *));
        argv[i] = calloc(strlen(token), sizeof(char));
        strcpy(argv[i++], token);
        token = strtok(NULL, sep);
    }
    return argv;
}

char *readFile(char *filename)
{
    /* 
    get contents of file
    don't forget to free the buffer
    */
    char *buffer = 0;
    long length;
    FILE *f = fopen(filename, "r");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = malloc(length);
        if (buffer)
        {
            fread(buffer, 1, length, f);
        }
        fclose(f);
    }
    return buffer;
}

void file_event_service(char *pathname, void (*callback_function)())
{
    FILE *fp;
    fp = fopen(pathname, "w");
    fclose(fp);
    /*
    watch file changes, if the file changed,
    start callback function in new thread
    */
    printf("[DEBUG] File event service started on file %s \n", pathname);
    int BUF_LEN = (10 * (sizeof(struct inotify_event) + _PC_NAME_MAX + 1));

    char buf[BUF_LEN];
    int inotify_fd = 0;
    struct inotify_event *event = NULL;

    inotify_fd = inotify_init();
    inotify_add_watch(inotify_fd, pathname, IN_ALL_EVENTS);
    while (1)
    {
        int n = read(inotify_fd, buf, BUF_LEN);
        char *p = buf;
        while (p < buf + n)
        {
            event = (struct inotify_event *)p;
            uint32_t mask = event->mask;
            if (mask & IN_CLOSE_WRITE)
            {
                pthread_t thread_id;
                pthread_create(&thread_id, NULL, callback_function, NULL);
                pthread_join(thread_id, NULL);
            }
            p += sizeof(struct inotify_event) + event->len;
        }
    }
}

void write_motor_status(int status)
{
    FILE *fp;
    fp = fopen(STATUS_FILE, "w");
    if (status == 1)
    {
        fprintf(fp, "1\n");
    }
    else
    {
        fprintf(fp, "0\n");
    }
    fclose(fp);
}

void write_motor_position()
{
    printf("[DEBUG] Writing motor position: %d %d \n", H_POSITION, V_POSITION);
    FILE *fp;
    fp = fopen(POSITION_FILE, "w");
    fprintf(fp, "%d %d \n", H_POSITION, V_POSITION);
    fclose(fp);
}

void motor_move(direction, steps)
{
    printf("[DEBUG] H_POSITION: %d\n", H_POSITION);
    printf("[DEBUG] V_POSITION: %d\n", H_POSITION);
    int motor_direction;
    switch (direction)
    {
    case PAN_FORWARD:
        if (H_POSITION + steps > H_MAX)
        {
            motor_goto(H_MAX, V_POSITION);
            write_motor_status(1);
            printf("[DEBUG] MAX H! \n");
            return;
        }
        motor_direction = PAN_FORWARD;
        write_motor_status(0);
        H_POSITION += steps;
        break;
    case PAN_REVERSE:
        if (H_POSITION - steps < H_MIN)
        {
            motor_goto(H_MIN, V_POSITION);
            write_motor_status(1);
            printf("[DEBUG] MIN H! \n");
            return;
        }
        motor_direction = PAN_REVERSE;
        write_motor_status(0);
        H_POSITION -= steps;
        break;
    case TILT_FORWARD:
        if (V_POSITION + steps > V_MAX)
        {
            motor_goto(H_POSITION, V_MAX);
            write_motor_status(1);
            printf("[DEBUG] MAX V! \n");
            return;
        }
        motor_direction = TILT_FORWARD;
        write_motor_status(0);
        V_POSITION += steps;
        break;
    case TILT_REVERSE:
        if (V_POSITION - steps < V_MIN)
        {
            motor_goto(H_POSITION, V_MIN);
            write_motor_status(1);
            printf("[DEBUG] MIN V! \n");
            return;
        }
        motor_direction = TILT_REVERSE;
        write_motor_status(0);
        V_POSITION -= steps;
        break;
    }
    miio_motor_move(motor_direction, steps);
    write_motor_position();
}

void motor_goto(int hor, int ver)
{
    printf("[DEBUG] Going to: %d %d \n", hor, ver);
    int steps_hor = H_POSITION - hor;
    int steps_ver = V_POSITION - ver;
    if (steps_hor < 0)
    {
        miio_motor_move(PAN_FORWARD, abs(steps_hor));
    }
    else
    {
        miio_motor_move(PAN_REVERSE, steps_hor);
    }
    if (steps_ver < 0)
    {
        miio_motor_move(TILT_FORWARD, abs(steps_ver));
    }
    else
    {
        miio_motor_move(TILT_REVERSE, steps_ver);
    }
    H_POSITION = hor;
    V_POSITION = ver;
    write_motor_position();
}

void callback_motor()
{
    /* 
    this function will called every time 
    the event file will modify.
    after that we read the value from file and then 
    controlling the motor with those values */

    char *contents = readFile(EVENT_FILE);
    char **argv = split(contents, " ");
    free(contents);

    int direction, steps = 0;

    if (strcmp(argv[0], "calibrate") == 0)
    {
        motor_calibrate();
    }
    else if (strcmp(argv[0], "get-status") == 0)
    {
        motor_h_position_get();
    }
    else if (strcmp(argv[0], "goto") == 0)
    {
        motor_goto((atoi(argv[1]) * H_MIN) / 100, (atoi(argv[2]) * V_MAX) / 100);
    }
    else if (strcmp(argv[0], "pan") == 0)
    {
        steps = atoi(argv[2]);
        if (strcmp(argv[1], "forward") == 0)
        {
            motor_move(PAN_FORWARD, steps);
        }
        else
        {
            motor_move(PAN_REVERSE, steps);
        }
    }
    else if (strcmp(argv[0], "tilt") == 0)
    {
        steps = atoi(argv[2]);
        if (strcmp(argv[1], "forward") == 0)
        {
            motor_move(TILT_FORWARD, steps);
        }
        else
        {
            motor_move(TILT_REVERSE, steps);
        }
    }
    else
    {
        printf("[DEBUG] Unrecognized command. \n", H_POSITION, V_POSITION);
    }
    free(argv);
}

void reset_motor()
{
    H_POSITION = 0;
    V_POSITION = 0;
    write_motor_status(0);
}

void motor_calibrate()
{
    printf("[DEBUG] Calibrating motor...\n");
    //Set internal position to MAX without moving to make sure the functions allow a max # of steps

    //calibrate horizontal axis first, right is 0. Move to center afterwards
    miio_motor_move(PAN_FORWARD, H_MAX + abs(H_MIN) + 10);
    H_POSITION = H_MAX;
    //calibrate vertical axis, down is 0. Move to center afterwards
    miio_motor_move(TILT_FORWARD, V_MAX + abs(V_MIN) + 4);
    V_POSITION = V_MAX;
}

void restore_last_position()
{
    printf("[DEBUG] Trying to fecth last position.\n");
    char *contents = readFile(POSITION_FILE);
    if (contents > 0)
    {
        char **argv = split(contents, " ");
        const hor = atoi(argv[0]);
        const ver = atoi(argv[1]);
        free(argv);
        printf("[DEBUG] Going to last position: %d %d \n", hor, ver);
        motor_goto(hor, ver);
    }
    else
    {
        printf("[DEBUG] No position saved, going to origin.\n");
        motor_goto(H_CENTER, V_CENTER);
    }
    free(contents);
}

void dl_load(void *handle)
{
    *(void **)(&motor_init) = dlsym(handle, "motor_init");
    *(void **)(&motor_exit) = dlsym(handle, "motor_exit");

    *(void **)(&motor_h_dir_set) = dlsym(handle, "motor_h_dir_set");
    *(void **)(&motor_h_position_get) = dlsym(handle, "motor_h_position_get");
    *(void **)(&motor_h_dist_set) = dlsym(handle, "motor_h_dist_set");
    *(void **)(&motor_h_move) = dlsym(handle, "motor_h_move");
    *(void **)(&motor_h_stop) = dlsym(handle, "motor_h_stop");

    *(void **)(&motor_v_dir_set) = dlsym(handle, "motor_v_dir_set");
    *(void **)(&motor_v_position_get) = dlsym(handle, "motor_v_position_get");
    *(void **)(&motor_v_dist_set) = dlsym(handle, "motor_v_dist_set");
    *(void **)(&motor_v_move) = dlsym(handle, "motor_v_move");
    *(void **)(&motor_v_stop) = dlsym(handle, "motor_v_stop");
}

int main(int argc, char *argv[])
{
    void *handle;
    handle = dlopen("libdevice_kit.so", RTLD_LAZY);
    if (!handle)
    {
        /* fail to load the library */
        fprintf(stderr, "Error: %s\n", dlerror());
        return EXIT_FAILURE;
    }
    if ( argc != 4 )
    {
        fprintf(stdout, "usage:\n");
        fprintf(stdout, "motord <EVENT file> <POSITION file> <STATUS file>\n");
        
        return EXIT_FAILURE;
    }
    EVENT_FILE = argv[1];
    POSITION_FILE = argv[2];
    STATUS_FILE = argv[3]; 

    dl_load(handle);
    motor_init();

    motor_calibrate();
    restore_last_position();
    file_event_service(EVENT_FILE, callback_motor);

    motor_exit();
    dlclose(handle);
    return EXIT_SUCCESS;
}
