/* Program simulating CSMC by Jonathan Perry and Mehroos Ali
*   
*/
#define _BSD_SOURCE
#include <stdlib.h>     //for randoms
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <unistd.h>

//Student structure for 
struct studentNode {
    int id;
    int tutor_id;
    sem_t studentTutoring;  //possibly going to replace students array
    int timesHelped;
    struct studentNode *next;
};

//GLOBAL VARIABLES
int chairs;
int helpsNeeded;
sem_t studentQueued;        //+Sem for Coord telling Tutors of a student: init 0, coord++, tutor--
sem_t studentSeated;        //+Sem for Student telling Coord: init 0, student++, coord--
sem_t openLobbyChairs;      // Sem for Lobby chairs: init #chairs, student++, student--
sem_t csmcOperating;        // Sem for Students to tell Main they are all done
int seatedStart = 0;        //end index, start index, both locked.
int seatedEnd = 0;
int remainingStudents;      // Number of students left
pthread_mutex_t remainingStudentsLock;
int currentlyTutored = 0;
int totalTutored = 0;
pthread_mutex_t tutoredCountingLock;
int studentNumber = 1;
pthread_mutex_t studentNumberLock;
int tutorNumber = 1;
pthread_mutex_t tutorNumberLock;

struct timespec programmingTime;
struct timespec tutoringTime;

struct studentNode* prioritizedStudents = NULL;
int prioritizedStudentsSize = 0;
pthread_mutex_t prioritizedLock;
struct studentNode* seatedStudents = NULL;
int seatedStudentsSize = 0;
pthread_mutex_t seatedLock;
float tutorTime = 200000;
float programingTime = 2000000;

/* Unmet Requirements:

    Data structures are dynamically allocated so that any number works
    Have specified outputs
    Errors go to stderr, not output
    Add appropriate errors

    Additional Hints:
    For details on how to use pthreads, synchronization primitives mutex and
    semaphores see man pages.For a more detailed tutorial on Pthreads and Semaphores,
    see https://computing.llnl.gov/tutorials/pthreads/#Thread
    You may also want to look at the solution for sleeping barber problem.
    It will give you some clues about how to solve the problem this project poses.
*/

/*Student Function
    This describes what the method does
*/
void* student(void* v)  {
    //pasing student id as argument to student thread.
    pthread_mutex_lock(&studentNumberLock);
    int id_student = studentNumber;
    studentNumber++;
    pthread_mutex_unlock(&studentNumberLock);
    //DEBUG
    //printf("Student %d started\n", id_student);
    //set-up the student struct for this student
    struct studentNode* thisStudent = (struct studentNode*) malloc(sizeof(struct studentNode));
    sem_init(&thisStudent->studentTutoring, 0, 0);
    thisStudent->timesHelped=0;
    thisStudent->id = id_student;
    int unoccupiedChairs;

    //students are always students...
    for(;;) {
        //start by programming
        thisStudent->next = NULL;
        //nanosleep(&programmingTime, NULL);
        usleep(programingTime);
        if(sem_trywait(&openLobbyChairs)!=0) {
            //if not available (ret != 0) continue and loop
            printf("S: Student %d found no empty chair. Will try again later.\n",thisStudent->id);
            continue;
        }   
        //if found (ret==0)
        sem_getvalue(&openLobbyChairs, &unoccupiedChairs);
        if(unoccupiedChairs<=0)   {
            unoccupiedChairs=0;
        }
        //Take seat and notify coordinator semaphore
        pthread_mutex_lock(&seatedLock);
        printf("S: Student %d takes a seat. Empty chairs = %d.\n", thisStudent->id, unoccupiedChairs);
        if(seatedStudents==NULL)    {
            //DEBUG
            //printf("Seated Students Empty\n");
            seatedStudents = thisStudent;
        } else  {
            //DEBUG
            //printf("Student %d seated first\n", seatedStudents->id);
            struct studentNode *ptr = seatedStudents;
            while(ptr->next!=NULL)   {
                ptr = ptr->next;
            }
            ptr->next = thisStudent;
        }
        seatedStudentsSize++;
        pthread_mutex_unlock(&seatedLock);
        sem_post(&studentSeated);
        //semwait on tutor.
        sem_wait(&thisStudent->studentTutoring);
        //student has now been helped. need to get tutor id?
        printf("S: Student %d received help from Tutor %d.\n", thisStudent->id, thisStudent->tutor_id);
        //release chair
        sem_post(&openLobbyChairs);
        thisStudent->timesHelped++;
        if(thisStudent->timesHelped>=helpsNeeded)   {
            pthread_mutex_lock(&remainingStudentsLock);
            remainingStudents--;
            //DEBUG
            //printf("--Student %d finished tutoring-- %d left\n", thisStudent->id, remainingStudents);
            pthread_mutex_unlock(&remainingStudentsLock);
            if(remainingStudents<=0)    {
                exit(0);
            }
            pthread_exit(NULL);
        }
    }
}

/*Coordinator Function
    Queues waiting students based on priority, and notifies tutors
*/
void* coordinator(void* v)  {
    int requests = 0;
    for(;;) {
        //wait on student semaphore to seat them
        sem_wait(&studentSeated);
        requests++;
        //get student from queue
        pthread_mutex_lock(&seatedLock);
        struct studentNode* justSeated = seatedStudents;
        seatedStudents = justSeated->next;
        seatedStudentsSize--;
        pthread_mutex_unlock(&seatedLock);
        //put student into prioritizedQueue
        pthread_mutex_lock(&prioritizedLock);
        if(prioritizedStudents==NULL || justSeated->timesHelped<prioritizedStudents->timesHelped)   {
            //justSeated is highest priority
            justSeated->next = prioritizedStudents;
            prioritizedStudents = justSeated;
        } else  {
            struct studentNode* comparedStudent = prioritizedStudents;
            while(comparedStudent->next!=NULL && justSeated->timesHelped>=comparedStudent->next->timesHelped)    {
                comparedStudent = comparedStudent->next;
            }
            //justSeated is the new next
            justSeated->next = comparedStudent->next;
            comparedStudent->next = justSeated;
        }
        prioritizedStudentsSize++;
        pthread_mutex_unlock(&prioritizedLock);

        //here's an ouput line that is rather ambiguious as to what the values should be.
        int waiting = 0;
        pthread_mutex_lock(&prioritizedLock);
        waiting += prioritizedStudentsSize;
        pthread_mutex_unlock(&prioritizedLock);
        pthread_mutex_lock(&seatedLock);
        waiting += seatedStudentsSize;
        pthread_mutex_unlock(&seatedLock);
        
        printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d\n",
        justSeated->id, (helpsNeeded - justSeated->timesHelped), waiting, requests);

        //notify tutor semaphore that a student is available
        sem_post(&studentQueued);
    }
}

/*Tutor Function
    Either waiting for coordinator to alert of chairs being sat in, or tutoring
    Once woken up, finds highest priority and tutors.

    Hints:
    For tutoring, make both the student and the tutor thread sleep for 0.2 ms.
*/
void* tutor(void *v)    {
    pthread_mutex_lock(&tutorNumberLock);
    int id_tutor = tutorNumber;
    tutorNumber++;
    pthread_mutex_unlock(&tutorNumberLock);
    for(;;) {
        //wait on coordinator semaphore for queued student
        sem_wait(&studentQueued);
        //get the student
        pthread_mutex_lock(&prioritizedLock);
        struct studentNode* tutoredStudent = prioritizedStudents;
        prioritizedStudents = prioritizedStudents->next;
        prioritizedStudentsSize--;
        pthread_mutex_unlock(&prioritizedLock);
        //This is not locked, but really can't have more than one edit it at once.
        tutoredStudent->tutor_id = id_tutor;
        //update tutored student numbers and output
        pthread_mutex_lock(&tutoredCountingLock);
        currentlyTutored++;
        printf("T: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d\n",
        tutoredStudent->id, tutoredStudent->tutor_id, currentlyTutored, totalTutored);
        pthread_mutex_unlock(&tutoredCountingLock);
        //tutor him
        //nanosleep(&tutoringTime, NULL);
        usleep(tutorTime);
        //update tutored student numbers
        pthread_mutex_lock(&tutoredCountingLock);
        totalTutored++;
        currentlyTutored--;
        pthread_mutex_unlock(&tutoredCountingLock);
        //tell the student he's been tutored
        sem_post(&tutoredStudent->studentTutoring);
    }
}

int main(int argc, char *argv[])  {
    //take in arguments
    //TODO and do error checking
    int numStudents = atoi(argv[1]);
    int tutors = atoi(argv[2]);
    chairs = atoi(argv[3]);
    helpsNeeded = atoi(argv[4]);
    //programmingTime.tv_nsec = 2000000;
    //programmingTime.tv_sec = 0;
    //tutoringTime.tv_nsec = 200000;
    //tutoringTime.tv_sec = 0;

    //Set-up variables
    srand(time(0));
    sem_init(&studentQueued, 0, 0);
    sem_init(&studentSeated, 0, 0);
    sem_init(&openLobbyChairs, 0, chairs);
    sem_init(&csmcOperating, 0, 0);
    remainingStudents = numStudents;
    pthread_mutex_init(&remainingStudentsLock, NULL);
    pthread_mutex_init(&prioritizedLock, NULL);
    pthread_mutex_init(&seatedLock, NULL);
    pthread_mutex_init(&tutoredCountingLock, NULL);
    pthread_mutex_init(&studentNumberLock, NULL);
    pthread_mutex_init(&tutorNumberLock, NULL);

    //start threads
    pthread_t ret;
    pthread_create(&ret, NULL, coordinator, NULL);
    int i;
    for(i = 1; i<=tutors; i++)   {
        pthread_create(&ret, NULL, tutor, (void*) &i);
    }
    for(i = 1; i<=numStudents; i++)    {
        pthread_create(&ret, NULL, student, NULL);
    }

    //this should never get here
    sem_wait(&csmcOperating);
}
