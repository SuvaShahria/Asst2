#include "mymemorymanager.h"
#include "my_pthread_t.h"
#include "my_pthread.c"

void * myblock_ptr;
void * myswap_ptr;
static char free_list_memory[(sizeof(struct page_meta) + 32)*2048];
static page_meta_ptr frame_table[2048];
char first_call = 1;
context_node * current;
mementryPtr head;
mementryPtr middle;
mementryPtr page_meta_mem_head = NULL;
mementryPtr page_meta_mem_middle = NULL;
void * page_meta_mem_ptr;
free_frame_ptr free_queue_head, free_queue_back;

void * myallocate(unsigned int x, char * file, int line, req_type rt) {
	
	void * rtn_ptr;
	page_meta_ptr current_page_meta;
	//Change all uses of temp to ^
		
	if (first_call) {
		first_call = 0;
		
		signal(SIGSEGV, swap_handler);
		
		//May need to free later
		myblock_ptr = memalign( sysconf(_SC_PAGESIZE), 8388608);
		printf("myblock_ptr address: %p\n", myblock_ptr);
		//Swap space
		myswap_ptr = memalign( sysconf(_SC_PAGESIZE), 8388608*2);
		
		page_meta_mem_ptr = myswap_ptr + 16437248;
		
		int i;
		
		for (i = 0; i < 2048; i++) {
			free_frame_ptr temp_free = (free_frame_ptr)mymalloc(sizeof(struct free_frame), NULL, 0, free_list_memory, (sizeof(struct free_frame)+32)*2048);
			temp_free->page_frame = i;
			temp_free->next = NULL;
			page_enqueue(temp_free);
			frame_table[i] = NULL;
		}
	}
	
	//If the current thread has a page
	if (current->thread_block->first_page != NULL) {
		head = current->thread_block->head;
		middle = current->thread_block->middle;
	} else { //Current thread does not have its first page yet
		
		//Page free in physical memory
		if (free_queue_head != NULL) {
			swap_pages(0, -1);
			head = page_meta_mem_head;
			middle = page_meta_mem_middle;
			current_page_meta = (page_meta_ptr)mymalloc(sizeof(struct page_meta), NULL, 0, page_meta_mem_ptr, 83*4096);
			current_page_meta->page_frame = 0;
			current_page_meta->in_pm = '1';
			current_page_meta->tid = current->thread_block->tid;
			current_page_meta->next = NULL;
			current_page_meta->head = NULL;
			current_page_meta->middle = NULL;
			current->thread_block->first_page = current_page_meta;
			frame_table[0] = current_page_meta;
		} else {//No free pages in physical memory, check swap file
			//check if swapfile is full
		}
		head = NULL;
		middle = NULL;
	}
	
	rtn_ptr = mymalloc(x, file, line, myblock_ptr + current->thread_block->malloc_frame*4096, 4096);
	current_page_meta->head = head;
	current_page_meta->middle = middle;
	
	//If there was no space in the page for the request...
	if (rtn_ptr == NULL) {
		//allocate new page from free list
		if (free_queue_head != NULL) {
			current->thread_block->malloc_frame++;
			
			//Swaps the page after the current page to free space for contiguous pages
			swap_pages(current->thread_block->malloc_frame, -1);
			
			head = page_meta_mem_head;
			middle = page_meta_mem_middle;
			current_page_meta = (page_meta_ptr)mymalloc(sizeof(struct page_meta), NULL, 0, page_meta_mem_ptr, 83*4096);
			current_page_meta->page_frame = current->thread_block->malloc_frame;
			current_page_meta->in_pm = '1';
			current_page_meta->tid = current->thread_block->tid;
			current_page_meta->next = NULL;
			current_page_meta->head = NULL;
			current_page_meta->middle = NULL;
			frame_table[current->thread_block->malloc_frame] = current_page_meta;
			frame_table[current->thread_block->malloc_frame-1]->next = current_page_meta;
		} else {
			//check if swapfile is full
		}
		head = NULL;
		middle = NULL;
		
		rtn_ptr = mymalloc(x, file, line, myblock_ptr + current->thread_block->malloc_frame*4096, 4096);
		current_page_meta->head = head;
		current_page_meta->middle = middle;
	}
	return rtn_ptr;
}

int mydeallocate(void * x, char * file, int line, req_type rt){
	
	head = current->thread_block->head;
	middle = current->thread_block->middle;
	
	//Check to see if proper page is in frame. Swap if not
	int mem_offset = (int)(x - myblock_ptr);
	int index = mem_offset / 4096;
	int i;
	
	if (frame_table[index]->tid == current->thread_block->tid) {
		//Check myfree return value for errors
		head = frame_table[index]->head;
		middle = frame_table[index]->middle;
		myfree(x, file, line);
	} else {
		page_meta_ptr temp = current->thread_block->first_page;
		
		for (i = 0; i < index; i++) {
			temp = temp->next;
		}
		
		//Assuming temp is in physical memory
		swap_pages(index, temp->page_frame);
		head = temp->head;
		middle = temp->middle;
		myfree(x, file, line);
	}
	
	//How do we tell a thread that one of its pages has free space if it has multiple pages
	
	return 0;
}
/*
int main(){
	
	unsigned int sz = sysconf(_SC_PAGESIZE);
	printf("%d\n", sz);
	
	printf("%d\n", (int)sizeof(struct mementry));
	printf("%d\n", (int)sizeof(struct page_meta));
	
	return 0;
}*/

/* Helper Functions */
void page_enqueue(free_frame_ptr node){
	if (free_queue_head != NULL) {
		free_queue_back->next = node;
		free_queue_back = node;
	} else {
		free_queue_head = node;
		free_queue_back = node;
	}
}

free_frame_ptr page_dequeue(){
	free_frame_ptr temp = free_queue_head;
	free_queue_head = free_queue_head->next;
	temp->next = NULL;
	return temp;
}

void swap_handler(int signum) {
	
	/*
	 * Check page table to see if the page being accessed belongs to the current thread.
	 * 		if so, change that page's mprotect to allow read/write: mprotect( myblock_ptr + frame_number * 4096, 4096, PROT_READ | PROT_WRITE);
	 * 		else, find the page they want and swap pages
	 */
	
}

void swap_pages(int src_frame, int dest_frame) {
	
	if (dest_frame == -1) {
		//Swap to new frame from free_queue_head
		free_frame_ptr rtn_page = page_dequeue();
		memcpy(myblock_ptr + rtn_page->page_frame*4096, myblock_ptr + src_frame * 4096, 4096);
		
		//Update page table
		if (frame_table[src_frame] != NULL) {
			frame_table[src_frame]->page_frame = rtn_page->page_frame;
			frame_table[rtn_page->page_frame] = frame_table[src_frame];
			frame_table[src_frame] = NULL;
		}
		
	} else {
		void * temp[4096];
		
		memcpy(temp, myblock_ptr + src_frame * 4096, 4096);
		memcpy(myblock_ptr + src_frame * 4096, myblock_ptr + dest_frame * 4096, 4096);
		memcpy(myblock_ptr + dest_frame * 4096, temp, 4096);
		
		//Update page table
		page_meta_ptr temp_pm = frame_table[src_frame];
		frame_table[src_frame] = frame_table[dest_frame];
		frame_table[dest_frame] = temp_pm;
		frame_table[src_frame]->page_frame = src_frame;
		frame_table[dest_frame]->page_frame = dest_frame;
	}
	
}




















