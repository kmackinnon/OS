/**
 * Keith MacKinnon
 * 260460985
 * October 11, 2014
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdbool.h>

#define TC 5 	// client generates requests at interval of 5s

typedef struct {
	int size;
	int head;
	int tail;
	int *array;
} CircularArray;

CircularArray *buffer;

// we'll use three semaphores for synchronization
sem_t buffer_mutex;
sem_t buffer_requests;
sem_t buffer_empty_slots;

void *printer_thread(void *ptr) {
	
	int *data = (int *) ptr;
	int id = data[0];
	int pages = 0;

	int index;
	
	while (true) {

		sem_wait(&buffer_requests); // if no requests, wait to print
		sem_wait(&buffer_mutex); // check if other thread owns buffer

		// print the "head" of the array
		index = buffer->tail;
		pages = buffer->array[index];
		printf("Printer %d printing %d pages from Buffer[%d]\n", id, pages, index);
		buffer->tail = (index + 1) % buffer->size;

		sem_post(&buffer_mutex); // free the buffer
		sem_post(&buffer_empty_slots); // increment number of empty spaces

		sleep(pages); // out of critical section, print the pages

		printf("Printer %d finishes printing %d pages from Buffer[%d]\n", id, pages, index);
		
	}

}

void *client_thread(void *ptr) {

	int *data = (int *) ptr;
	int id = data[0];
	int pages = data[1];

	int index;

	while (true) {
		
		sem_wait(&buffer_empty_slots); // if buffer full, wait to request
		sem_wait(&buffer_mutex); // ensure ownership of buffer

		index = buffer->head;
	
		printf("Client %d has %d pages to print, puts request in Buffer[%d]\n", id, pages, index);
		buffer->array[index] = pages;
		buffer->head = (index + 1) % buffer->size;

		sem_post(&buffer_mutex); // free the buffer
		sem_post(&buffer_requests); // increment number of print requests

		sleep(TC); // wait 5 seconds before the next print job 

		printf("Client %d wakes up, puts request in Buffer[%d]\n", id, index);
	}
}

int main(int argc, char const *argv[]) {
	
	// make sure user inputs correct number of parameters
	if (argc != 4) {
		printf("Usage: %s clients printers buffer_size\n", argv[0]);
		exit(-1);
	}

	int num_clients = atoi(argv[1]);
	int num_printers = atoi(argv[2]);
	int buffer_size = atoi(argv[3]);

	// C,P,B > 0
	if (num_clients <= 0 || num_printers <= 0 || buffer_size <= 0) {
		printf("Parameters must be positive integers\n");
		exit(-1);
	}

	// initialize the semaphores
	sem_init(&buffer_mutex, 0, 1);
	sem_init(&buffer_requests, 0, 0);
	sem_init(&buffer_empty_slots, 0, buffer_size);

	// initialize printer buffer 
	buffer = malloc(sizeof(CircularArray));
	buffer->array = malloc(buffer_size);
	buffer->size = buffer_size;
	buffer->head = 0;
	buffer->tail = 0;

	srand(time(NULL)); // random seed

	pthread_t clients[num_clients];
	pthread_t printers[num_printers];

	int i;
	int *data; // will store id and number of pages

	// create client threads
	for (i = 0; i < num_clients; i++) {
		data = malloc(2);
		data[0] = i;
		data[1] = (rand() % 10) + 1; // set random range 1-10
		pthread_create(&clients[i], NULL, client_thread, (void *) data);
	}

	//  create printer threads
	for (i = 0; i < num_printers; i++) {
		data = malloc(1);
		data[0] = i;
		pthread_create(&printers[i], NULL, printer_thread, (void *) data);
	}

	// join printer and client threads
	// need to ensure parent doesn't exit before thread completes
	for (i = 0; i < num_clients; i++) {
		pthread_join(clients[i], NULL);
	}

	for (i = 0; i < num_printers; i++) {
		pthread_join(printers[i], NULL);
	}

	return 0;
}
