#include "workflow_scheduler.h"
//#include "extrae_user_events.h"

int global_status = WORKFLOW_STATUS_FINISHED;

//----------------------------------------------------------------------------------------
//  work_item
//----------------------------------------------------------------------------------------

work_item_t *work_item_new(int stage_id, void *data) {
     work_item_t *wi = calloc(1, sizeof(work_item_t));
     
     wi->stage_id = stage_id;
     wi->data = data;
     
     wi->context = NULL;
     
     return wi;
}

//----------------------------------------------------------------------------------------

void work_item_free(work_item_t *wi) {
     if (wi) free(wi);
}

//----------------------------------------------------------------------------------------
// workflow functions
//----------------------------------------------------------------------------------------

// Private Functions
//----------------------------------------------------------------------------------------

int workflow_get_status_extra_(workflow_t *wf);
int workflow_get_status_(workflow_t *wf);

//----------------------------------------------------------------------------------------

workflow_t *workflow_new() {
     workflow_t *wf = calloc(1, sizeof(workflow_t));

     wf->num_threads = 0;
     wf->max_num_work_items = 0;

     wf->num_stages = 0;
     wf->completed_producer = 0;
     
     wf->num_pending_items = 0;
     
     wf->running_producer = 0;
     wf->running_consumer = 0;
     
     pthread_mutex_init(&wf->producer_mutex, NULL);
     pthread_mutex_init(&wf->consumer_mutex, NULL);
     
     pthread_mutex_init(&wf->main_mutex, NULL);
     
     wf->pending_items = NULL;
     wf->completed_items = array_list_new(100, 1.25f, COLLECTION_MODE_ASYNCHRONIZED);
     
     wf->stage_functions = NULL;
     wf->stage_labels = NULL;

     wf->producer_function = NULL;
     wf->producer_label = NULL;
     
     wf->consumer_function = NULL;
     wf->consumer_label = NULL;
     
     wf->complete_extra_stage = 1;
     //wf->status_function = workflow_get_status_;

     return wf;
}

//----------------------------------------------------------------------------------------

void workflow_free(workflow_t *wf) {
     if (wf == NULL) return;
     
     if (wf->pending_items) {
	  for (int i = 0; i < wf->num_stages; i++) {
	       array_list_free(wf->pending_items[i], NULL);
	  }
	  free(wf->pending_items);
     }
     
     if (wf->completed_items) array_list_free(wf->completed_items, NULL);
     
     if (wf->num_stages && wf->stage_labels) {
	  for (int i = 0; i < wf->num_stages; i++) {
	       if (wf->stage_labels[i]) {
		    free(wf->stage_labels[i]);
	       }
	  }
	  free(wf->stage_labels);
     }
     
     if (wf->producer_label) {
	  free(wf->producer_label);
     }
     
     if (wf->consumer_label) {
	  free(wf->consumer_label);
     }
     
     free(wf);
}

//----------------------------------------------------------------------------------------
/*
void workflow_change_status_function(workflow_t *wf) {
  wf->status_function = workflow_get_status_extra_;
}
*/
//----------------------------------------------------------------------------------------

void workflow_set_stages(int num_stages, workflow_stage_function_t *functions, 
			 char **labels, workflow_t *wf) {
     
     if (functions && wf) {
	  pthread_mutex_lock(&wf->main_mutex);
	  
	  wf->num_stages = num_stages;
	  wf->stage_functions = functions;
	  wf->pending_items = (array_list_t **) calloc(num_stages, sizeof(array_list_t *));
	  
	  if (labels) wf->stage_labels = (char **) calloc(num_stages, sizeof(char *));
	  
	  for (int i = 0; i < num_stages; i++) {
	       wf->pending_items[i] = array_list_new(100, 1.25f, COLLECTION_MODE_ASYNCHRONIZED);
	       if (labels && labels[i]) wf->stage_labels[i] = strdup(labels[i]);
	  }
	  
	  pthread_mutex_unlock(&wf->main_mutex);
     }
}

//----------------------------------------------------------------------------------------

void workflow_set_producer(workflow_producer_function_t *function, 
			   char *label, workflow_t *wf) {
     if (function && wf) {
	  pthread_mutex_lock(&wf->main_mutex);
	  
	  wf->producer_function = function;
	  
	  if (label) wf->producer_label = strdup(label);
	  
	  pthread_mutex_unlock(&wf->main_mutex);
     }
}

//----------------------------------------------------------------------------------------

void workflow_set_consumer(workflow_consumer_function_t *function, 
			   char *label, workflow_t *wf) {
     if (function && wf) {
	  pthread_mutex_lock(&wf->main_mutex);
	  
	  wf->consumer_function = function;
	  
	  if (label) wf->consumer_label = strdup(label);
	  
	  pthread_mutex_unlock(&wf->main_mutex);
     }
}

//----------------------------------------------------------------------------------------
int workflow_get_num_items_(workflow_t *wf) {
     return wf->num_pending_items + array_list_size(wf->completed_items);
}

int workflow_get_num_items(workflow_t *wf) {
     int ret = 0;
     pthread_mutex_lock(&wf->main_mutex);
     
     ret = workflow_get_num_items_(wf);
     
     pthread_mutex_unlock(&wf->main_mutex);
     return ret;
}

//----------------------------------------------------------------------------------------

int workflow_get_num_items_at(int stage_id, workflow_t *wf) {
     int ret = 0;
     pthread_mutex_lock(&wf->main_mutex);
     
     ret = array_list_size(wf->pending_items[stage_id]);
     
     pthread_mutex_unlock(&wf->main_mutex);
     return ret;
}

//----------------------------------------------------------------------------------------

int workflow_get_num_completed_items_(workflow_t *wf) {
     return array_list_size(wf->completed_items);
}

//----------------------------------------------------------------------------------------

int workflow_get_num_completed_items(workflow_t *wf) {
     int ret = 0;
     pthread_mutex_lock(&wf->main_mutex);
     
     ret = workflow_get_num_completed_items_(wf);
     
     pthread_mutex_unlock(&wf->main_mutex);
     return ret;
}

//----------------------------------------------------------------------------------------

int workflow_is_producer_finished(workflow_t *wf) {
     int ret = 0;
     pthread_mutex_lock(&wf->main_mutex);
     
     ret = wf->completed_producer;
     
     pthread_mutex_unlock(&wf->main_mutex);
     return ret;
}
//----------------------------------------------------------------------------------------

void workflow_insert_item(void *data, workflow_t *wf) {
     workflow_insert_item_at(0, data, wf);
}

//----------------------------------------------------------------------------------------

void workflow_insert_item_at(int stage_id, void *data, workflow_t *wf) {
     work_item_t *item = work_item_new(stage_id, data);
     
     pthread_mutex_lock(&wf->main_mutex);
     //printf("Producer mutex lock\n");
     while (workflow_get_num_items_(wf) >= wf->max_num_work_items) {
       //printf("Wait insert...\n");
	  pthread_cond_wait(&wf->producer_cond, &wf->main_mutex);
     }
     //printf("Insert continue\n");
     if (array_list_insert(item, wf->pending_items[stage_id])) {
	  wf->num_pending_items++;
	  item->context = (void *) wf;
     }
     
     pthread_mutex_unlock(&wf->main_mutex);
}

//----------------------------------------------------------------------------------------

void *workflow_remove_item(workflow_t *wf) {
     void *ret = NULL;
     work_item_t *item;
     
     pthread_mutex_lock(&wf->main_mutex);
     //printf("Writer mutex lock\n");
     while (workflow_get_num_completed_items_(wf) <= 0) {
       //printf("Cond wait completed items...\n");
	  pthread_cond_wait(&wf->consumer_cond, &wf->main_mutex);
     }
     //printf("writer continue\n");
     item = array_list_remove_at(0, wf->completed_items);
      
     pthread_cond_broadcast(&wf->producer_cond);
    
     pthread_mutex_unlock(&wf->main_mutex);
     
     if (item) {
	  ret = item->data;
	  work_item_free(item);
     }
     
     return ret;
}

//----------------------------------------------------------------------------------------
/*
void *workflow_remove_item_at(int stage_id, workflow_t *wf) {
  void *ret = NULL;
  work_item_t *item;

  while (1) {
    if (workflow_get_status(wf) == WORKFLOW_STATUS_RUNNING &&
	workflow_get_num_completed_items(wf) > 0) {

      pthread_mutex_lock(&wf->main_mutex);
      item = array_list_remove_at(0, wf->pending_items[stage_id]);
      if (item) {
	wf->num_pending_items--;
	pthread_mutex_unlock(&wf->main_mutex);
	ret = item->data;
	work_item_free(item);
	break;
      }
      pthread_mutex_unlock(&wf->main_mutex);
    }
    waitfor(500); // wait for 500 msec
  }

  return ret;
}
*/
//----------------------------------------------------------------------------------------

int workflow_get_status_(workflow_t *wf) {
     int ret = WORKFLOW_STATUS_FINISHED;
     
     if ( (!wf->completed_producer) ||
	  (wf->num_pending_items)   ||
	  (array_list_size(wf->completed_items) > 0) || 
	  (!wf->complete_extra_stage)) {
          
	  ret = WORKFLOW_STATUS_RUNNING;
     } 
 
     return ret;
}

//----------------------------------------------------------------------------------------

int workflow_get_simple_status(workflow_t *wf) {
     int ret = WORKFLOW_STATUS_FINISHED;

     pthread_mutex_lock(&wf->main_mutex);
     if ( (!wf->completed_producer) ||
	  (wf->num_pending_items)   ||
	  (array_list_size(wf->completed_items) > 0)) {
          
	  ret = WORKFLOW_STATUS_RUNNING;
     } 
     pthread_mutex_unlock(&wf->main_mutex);

     //global_status = ret;
 
     return ret;
}

//------------------------------------------------------------------------------------------

int workflow_get_status(workflow_t *wf) {
  int ret;
  pthread_mutex_lock(&wf->main_mutex);
  ret = workflow_get_status_(wf);//wf->status_function(wf);//
  pthread_mutex_unlock(&wf->main_mutex);
  return ret;
}

//----------------------------------------------------------------------------------------

void workflow_producer_finished(workflow_t *wf) {
     pthread_mutex_lock(&wf->main_mutex);
     wf->completed_producer = 1;
     pthread_cond_broadcast(&wf->workers_cond);
     pthread_mutex_unlock(&wf->main_mutex);
}

//----------------------------------------------------------------------------------------

void workflow_run(void *input, workflow_t *wf) {
     workflow_run_with(sysconf(_SC_NPROCESSORS_ONLN), input, wf); 
}

//----------------------------------------------------------------------------------------
void workflow_insert_stage_item_at(void *data, int new_stage, workflow_t *wf) {
     work_item_t *item = work_item_new(new_stage, data);
    
     pthread_mutex_lock(&wf->main_mutex);
     array_list_insert(item, wf->pending_items[item->stage_id]);
     pthread_mutex_unlock(&wf->main_mutex);
}

//----------------------------------------------------------------------------------------

void workflow_schedule(workflow_t *wf) {
     work_item_t *item = NULL;

     pthread_mutex_lock(&wf->main_mutex);

     //printf("Workflow schedule mutex lock\n");
     while (wf->num_pending_items <= 0 && 
	    !wf->completed_producer) {
       //printf("Waitign in workflow...\n");
       pthread_cond_wait(&wf->workers_cond, &wf->main_mutex);
     }

//     for (int i = wf->num_stages - 1; i >= 0; i--) {
     for (int i = 0 ; i <= wf->num_stages - 1; i++) {
	  item = array_list_remove_at(0, wf->pending_items[i]);
	  if (item) {
	       break;
	  }
     }
     
     pthread_mutex_unlock(&wf->main_mutex);

     if (item) {
	  workflow_stage_function_t stage_function = wf->stage_functions[item->stage_id];

	  //	  Extrae_event(6000019, item->stage_id + 1); 
	  int next_stage = stage_function(item->data);
	  //	  Extrae_event(6000019, 0); 

	  item->stage_id = next_stage;
	  
	  if (next_stage >= 0 && next_stage < wf->num_stages) {	       
	       // moving item to the next stage to process
	       pthread_mutex_lock(&wf->main_mutex);
	       array_list_insert(item, wf->pending_items[item->stage_id]);
	       pthread_mutex_unlock(&wf->main_mutex);
	  } else if (next_stage == -1) {
	       // item fully processed !!
	       pthread_mutex_lock(&wf->main_mutex);
	       wf->num_pending_items--;
	       array_list_insert(item, wf->completed_items);

	       pthread_cond_broadcast(&wf->consumer_cond);
	       pthread_mutex_unlock(&wf->main_mutex);  
	  } else {
	       // error !!
	       pthread_mutex_lock(&wf->main_mutex);
	       wf->num_pending_items--;
	       pthread_mutex_unlock(&wf->main_mutex);	       
	  }
     }
}

//----------------------------------------------------------------------------------------

int workflow_lock_producer(workflow_t *wf) {
     int ret = 0;
     pthread_mutex_lock(&wf->producer_mutex);
     if (wf->running_producer) {
	  ret = 0;
     } else {
	  ret = 1;
	  wf->running_producer = 1;
     }
     pthread_mutex_unlock(&wf->producer_mutex);
     return ret;
}

int workflow_unlock_producer(workflow_t *wf) {
  pthread_mutex_lock(&wf->producer_mutex);
  wf->running_producer = 0;
  pthread_mutex_unlock(&wf->producer_mutex);
}

int workflow_lock_consumer(workflow_t *wf) {
     int ret = 0;
     pthread_mutex_lock(&wf->consumer_mutex);
     if (wf->running_consumer) {
	  ret = 0;
     } else {
	  ret = 1;
	  wf->running_consumer = 1;
     }
     pthread_mutex_unlock(&wf->consumer_mutex);
     return ret;
}

int workflow_unlock_consumer(workflow_t *wf) {
  pthread_mutex_lock(&wf->consumer_mutex);
  wf->running_consumer = 0;
  pthread_mutex_unlock(&wf->consumer_mutex);
}

//----------------------------------------------------------------------------------------

typedef struct workflow_context {
//  int id;
  void *input;
  workflow_t *wf;
} workflow_context_t;

workflow_context_t *workflow_context_new(void *input, workflow_t *wf) {
//workflow_context_t *workflow_context_new(int id, void *input, workflow_t *wf) {
  workflow_context_t *c = calloc(1, sizeof(workflow_context_t));

//  c->id = id;
  c->input = input;
  c->wf = wf;

  return c;
}

void workflow_context_free(workflow_context_t *c) {
  if (c) free(c);
}

//----------------------------------------------------------------------------------------

void *thread_function(void *wf_context) {
  void *input = ((workflow_context_t *) wf_context)->input;
  workflow_t *wf = ((workflow_context_t *) wf_context)->wf;
  
  void *data = NULL;

  int num_threads = wf->num_threads;
  workflow_stage_function_t stage_function = NULL;
  workflow_producer_function_t producer_function = wf->producer_function;
  workflow_consumer_function_t consumer_function = wf->consumer_function;


  while (workflow_get_status(wf) == WORKFLOW_STATUS_RUNNING) {
    if (producer_function                        &&
	workflow_get_num_items(wf) < num_threads && 
	(!workflow_is_producer_finished(wf))     &&
	workflow_lock_producer(wf)) {
      
      //	 Extrae_event(6000019, 7); 
      data = producer_function(input);
      //	 Extrae_event(6000019, 0); 
      
      if (data) {
	workflow_insert_item(data, wf);
      } else {
	workflow_producer_finished(wf);
      }
      workflow_unlock_producer(wf);
      
    } else if (consumer_function                         &&
	       workflow_get_num_completed_items_(wf) > 0 && 
	       workflow_lock_consumer(wf)) {	 
	 if (data = workflow_remove_item(wf)) {
	   if (array_list_size(wf->completed_items) == 0 && 
	       !wf->num_pending_items && wf->completed_producer) {
	     global_status = WORKFLOW_STATUS_FINISHED;
	   }
	   consumer_function(data);
	 }
	 workflow_unlock_consumer(wf);
    } else {
      //printf("Worker\n");
      workflow_schedule(wf);
    }
  }

  

  //printf("Thread function end\n");
}

//----------------------------------------------------------------------------------------

void workflow_run_with(int num_threads, void *input, workflow_t *wf) {

      //     Extrae_init();

     wf->num_threads = num_threads;
     wf->max_num_work_items = num_threads * 3;
     
     pthread_t threads[num_threads];
     pthread_attr_t attr;
     
     int num_cpus = 64;
     int cpuArray[num_cpus];
     
     for (int i = 0; i < num_cpus; i++) {
	  cpuArray[i] = i;
     }
     
     pthread_attr_init(&attr);
     pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
     
     int ret;
     workflow_context_t *wf_context = workflow_context_new(input, wf);
     
     struct timeval start_time, stop_time;
     gettimeofday(&start_time, NULL);

     for(int i = 0; i < num_threads; i++){

	  cpu_set_t cpu_set;
	  CPU_ZERO( &cpu_set);
	  CPU_SET( cpuArray[i % num_cpus], &cpu_set);
	  sched_setaffinity(syscall(SYS_gettid), sizeof(cpu_set), &cpu_set);

//	  printf("***** creating thread %i...\n", i);
	  
	  if (ret = pthread_create(&threads[i], &attr, thread_function, (void *) wf_context)) {
	       printf("ERROR; return code from pthread_create() is %d\n", ret);
	       exit(-1);
	  }
     }

     // free attribute and wait for the other threads
     void *status;
     pthread_attr_destroy(&attr);
     for (int i = 0; i < num_threads; i++) {
	  if (ret = pthread_join(threads[i], &status)) {
	       printf("ERROR; return code from pthread_join() is %d\n", ret);
	       exit(-1);
	  }
     }

     gettimeofday(&stop_time, NULL);
     /*
     printf("\t\t---------------> Workflow time = %0.4f sec\n", 
	    (stop_time.tv_sec - start_time.tv_sec) + 
	    ((stop_time.tv_usec - start_time.tv_usec) / 1000000.0));
     */
     workflow_context_free(wf_context);
     //     Extrae_fini();
}

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------





