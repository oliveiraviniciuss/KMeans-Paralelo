//IMPORTS
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "kmeans.h"

//STRUCTURES
typedef struct thread_args
{
	int start_object;
	int num_objects;

}Thread_Args;

//GLOBALS
float **objects;
int    *membership;
float  **clusters;       /* out: [numClusters][numCoords] */
float  **newClusters;    /* [numClusters][numCoords] */
int     *newClusterSize; /* [numClusters]: no. objects assigned in each new cluster */
float    delta;          /* % of objects change their clusters */
int numClusters;
int numCoords;
int threads_no;
Thread_Args* threads_structure;
pthread_mutex_t lock;   //Mutex to lock region


//PROTOTYPES
void *assignMembership(void *args);

//FUNCTIONS
/*----< euclid_dist_2() >----------------------------------------------------*/
/* square of Euclid distance between two multi-dimensional points            */
__inline static
float euclid_dist_2(int    numdims,  /* no. dimensions */
                    float *coord1,   /* [numdims] */
                    float *coord2)   /* [numdims] */
{
    int i;
    float ans=0.0;

    for (i=0; i<numdims; i++)
        ans += (coord1[i]-coord2[i]) * (coord1[i]-coord2[i]);

    return(ans);
}


/*----< find_nearest_cluster() >---------------------------------------------*/
__inline static
int find_nearest_cluster(int     numClusters, /* no. clusters */
                         int     numCoords,   /* no. coordinates */
                         float  *object,      /* [numCoords] */
                         float **clusters)    /* [numClusters][numCoords] */
{
    int   index, i;
    float dist, min_dist;

    /* find the cluster id that has min distance to object */
    index    = 0;
    min_dist = euclid_dist_2(numCoords, object, clusters[0]);

    for (i=1; i<numClusters; i++) {
        dist = euclid_dist_2(numCoords, object, clusters[i]);
        /* no need square root */
        if (dist < min_dist) { /* find the min and its array index */
            min_dist = dist;
            index    = i;
        }
    }
    return(index);
}

/*----< seq_kmeans() >-------------------------------------------------------*/
/* return an array of cluster centers of size [numClusters][numCoords]       */
float** seq_threaded_kmeans(float **objects_,      /* in: [numObjs][numCoords] */
                   int     numCoords_,    /* no. features */
                   int     numObjs,      /* no. objects */
                   int     numClusters_,  /* no. clusters */
                   float   threshold,    /* % objects change membership */
                   int    *membership_,   /* out: [numObjs] */
                   int    *loop_iterations, 
                   int threads_no_)
{
    //Initialize local variables
    int      i, w, j, index, z, loop=0;
    
    //Assign global variables
    numClusters = numClusters_;
    numCoords = numCoords_;
    threads_no = threads_no_;
    objects = objects_;
    membership = membership_;
    

    /* allocate a 2D space for returning variable clusters[] (coordinates
       of cluster centers) */
    clusters    = (float**) malloc(numClusters *             sizeof(float*));
    assert(clusters != NULL);
    clusters[0] = (float*)  malloc(numClusters * numCoords * sizeof(float));
    assert(clusters[0] != NULL);
    for (i=1; i<numClusters; i++)
        clusters[i] = clusters[i-1] + numCoords;

    /* pick first numClusters elements of objects[] as initial cluster centers*/
    for (i=0; i<numClusters; i++)
        for (j=0; j<numCoords; j++)
            clusters[i][j] = objects[i][j];

    /* initialize membership[] */
    for (i=0; i<numObjs; i++) membership[i] = -1;

    /* need to initialize newClusterSize and newClusters[0] to all 0 */
    newClusterSize = (int*) calloc(numClusters, sizeof(int));
    assert(newClusterSize != NULL);

    newClusters    = (float**) malloc(numClusters *            sizeof(float*));
    assert(newClusters != NULL);
    newClusters[0] = (float*)  calloc(numClusters * numCoords, sizeof(float));
    assert(newClusters[0] != NULL);
    for (i=1; i<numClusters; i++)
        newClusters[i] = newClusters[i-1] + numCoords;

    /*Defining number of threads*/
    int objects_per_thread = (int) numObjs / threads_no;

    //Creating threads vector and structures
    pthread_t phys_threads[threads_no];
    int threads_id [threads_no];
    for (i = 0; i < threads_no; i ++) threads_id[i] = i;
    threads_structure = malloc (threads_no * sizeof (Thread_Args));    

    //Temporary acumulator
    int temp_acumulator = 0;

    //For each thread
    for (i = 0 ; i < (threads_no - 1); i ++)
    {
    	//Defines start object and quantity of objects
    	threads_structure[i].start_object = temp_acumulator;
    	threads_structure[i].num_objects = objects_per_thread;

    	temp_acumulator += objects_per_thread;
    }
	
	//Defines last thread start object and quantity of last objects
    threads_structure[threads_no - 1].start_object = temp_acumulator;
    threads_structure[threads_no - 1].num_objects = numObjs - temp_acumulator;

    if (pthread_mutex_init(&lock, NULL) != 0) 
    { 
        printf("\n mutex init has failed\n"); 

    }

    //Runs the iterations
    do {
        delta = 0.0;

        w = 0;
        //Create Threads (No critical region)
        while (w < threads_no)
	    {


	    	//pthread_mutex_lock(&lock); //Locks region
	    	//int z = w;

	        if (pthread_create(&(phys_threads[w]), NULL, assignMembership, (void*)(&threads_id[w])) != 0)
	        {
	          fprintf(stderr, "error: Cannot create thread # %d\n", w);
	          break;
	        }
	        
	        

	        w = w + 1;

	        //pthread_mutex_unlock(&lock); //Locks region

	    }

	    //Waits for threads to finish
        for (w = 0; w < threads_no; w ++)
        {
        	//pthread_join(phys_threads[w], NULL); 
        	if (pthread_join(phys_threads[w], NULL) != 0)
	        {
	          fprintf(stderr, "error: Cannot join thread # %d\n", w);
	        }
        }

        /* average the sum and replace old cluster centers with newClusters */
        for (i=0; i<numClusters; i++) {
            for (j=0; j<numCoords; j++) {
                if (newClusterSize[i] > 0)
                    clusters[i][j] = newClusters[i][j] / newClusterSize[i];
                newClusters[i][j] = 0.0;   /* set back to 0 */
            }
            newClusterSize[i] = 0;   /* set back to 0 */
        }
            
        delta /= numObjs;	//Calculates percentage of changes

    } while (delta > threshold && loop++ < 500);

    *loop_iterations = loop + 1;

    free(newClusters[0]);
    free(newClusters);
    free(newClusterSize);

    return clusters;
}

//Function to find clusters for each object and assing a membership for this object
void *assignMembership(void *args)
{

	//Local variables
	int  i, j, index;

	//Retrieving thread structure by given number of the thread
	Thread_Args thread_arg = threads_structure[*(int*) args];

	//Iterates over each object to assing its membership
	for (i=thread_arg.start_object; i<(thread_arg.start_object + thread_arg.num_objects); i++) 
	{
            /* find the array index of nestest cluster center */
            index = find_nearest_cluster(numClusters, numCoords, objects[i],
                                         clusters);

           // pthread_mutex_lock(&lock); //Locks region

            /* if membership changes, increase delta by 1 */
            if (membership[i] != index) delta += 1.0;

            /* assign the membership to object i */
            membership[i] = index;

            /* update new cluster centers : sum of objects located within */
            newClusterSize[index]++;
            for (j=0; j<numCoords; j++)
                newClusters[index][j] += objects[i][j];

           // pthread_mutex_unlock(&lock);   //Unlocks Region
        }

}

