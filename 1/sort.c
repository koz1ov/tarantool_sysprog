#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <time.h>
#include <aio.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct coro_struct
{
    ucontext_t context;
    clock_t last_timestamp;
    clock_t total_time;
} coro_struct;

typedef struct array_struct
{
    int *p_arr;
    size_t size;
} array_struct;

static coro_struct *coros;
static int curr_coro_i, files_count;

#define coro_yield() ({						\
    int old_i = curr_coro_i;    									\
    curr_coro_i = (curr_coro_i + 1) % files_count;					\
    coros[old_i].total_time += clock() - coros[old_i].last_timestamp; \
    coros[curr_coro_i].last_timestamp = clock();          			 \
    swapcontext(&coros[old_i].context, &coros[curr_coro_i].context); 				\
})

// subroutine for 'void merge_sort(int*, size_t)'
static void merge(int *array, size_t size)
{
    int *p_left = array, *p_right = array + size / 2;
    coro_yield();

    int *tmp_res = malloc(size * sizeof(int));
    coro_yield();
    int *p_tmp = tmp_res;
    coro_yield();

    while (p_left < array + size / 2 && p_right < array + size) {
        *p_tmp++ = *p_left <= *p_right ? *p_left++ : *p_right++;
        coro_yield();
    }
    while (p_left < array + size / 2) {
        *p_tmp++ = *p_left++;
        coro_yield();
    }
    while (p_right < array + size) {
        *p_tmp++ = *p_right++;
        coro_yield();
    }

    memcpy(array, tmp_res, size * sizeof(int));
    coro_yield();
    free(tmp_res);
}

// sorts the array using "Merge sort" algorithm
static void merge_sort(int *array, size_t size)
{
    if (size <= 1)
        return;
    coro_yield();

    merge_sort(array, size / 2);
    coro_yield();
    merge_sort(array + size / 2, size - size / 2);
    coro_yield();
    merge(array, size);
}

#define CHUNK_SIZE 1024

static char* read_file_async(char *filename)
{
    struct aiocb control_block;
    coro_yield();
    char *res_str = malloc(CHUNK_SIZE);
    coro_yield();
    control_block.aio_fildes = open(filename, O_RDONLY);
    coro_yield();
    control_block.aio_buf = res_str;
    coro_yield();
    control_block.aio_offset = 0;
    coro_yield();
    control_block.aio_reqprio = 0;
    coro_yield();
    control_block.aio_sigevent.sigev_notify = SIGEV_NONE;
    coro_yield();
    control_block.aio_nbytes = CHUNK_SIZE;
    coro_yield();

    aio_read(&control_block);
    coro_yield();

    while (1) {
        while (aio_error(&control_block)) // waiting for the request to finish
            coro_yield();

        int read_bytes = aio_return(&control_block);
        coro_yield();
        if (!read_bytes) { // end of file
            res_str = realloc(res_str, control_block.aio_offset + 1);
            coro_yield();
            res_str[control_block.aio_offset] = 0;
            coro_yield();
            close(control_block.aio_fildes);
            coro_yield();
            return res_str;
        }

        control_block.aio_offset += read_bytes;
        coro_yield();
        control_block.aio_nbytes = CHUNK_SIZE - control_block.aio_offset % CHUNK_SIZE;
        coro_yield();

        if (control_block.aio_offset % CHUNK_SIZE == 0)
            res_str = realloc(res_str, control_block.aio_offset + CHUNK_SIZE);
        coro_yield();
        control_block.aio_buf = res_str + control_block.aio_offset;
        coro_yield();

        aio_read(&control_block);
        coro_yield();
    }
}

// loads numbers from the file into memory and sorts them
// result is in res_arr after return
static void sort_file(char* filename, array_struct *res_arr)
{
    static int jobs_running = 0;
    jobs_running++;
    coro_yield();

    char *file_string = read_file_async(filename);
    coro_yield();
    FILE *file_string_stream = fmemopen(file_string, strlen(file_string), "r");
    coro_yield();

    size_t ints_count = 0, dummy;
    coro_yield();
    while (fscanf(file_string_stream, "%ld", &dummy) == 1) {
        ++ints_count;
        coro_yield();
    }

    int *array_to_sort = malloc(ints_count * sizeof(int));
    coro_yield();

    fseek(file_string_stream, 0, SEEK_SET);
    coro_yield();

    for (int i = 0; i < ints_count; ++i) {
        fscanf(file_string_stream, "%d", &array_to_sort[i]);
        coro_yield();
    }

    fclose(file_string_stream);
    coro_yield();
    free(file_string);
    coro_yield();

    merge_sort(array_to_sort, ints_count);
    coro_yield();

    res_arr->p_arr = array_to_sort;
    coro_yield();
    res_arr->size = ints_count;
    coro_yield();

    printf("Coro %d finished sorting\n", curr_coro_i);
    coro_yield();
    jobs_running--;
    coro_yield();

    while (jobs_running) // wait all coros
        coro_yield();
}

// merges array2 into array1 and frees array2
static void merge_arrays(array_struct *array1, array_struct *array2)
{
    int *p_left = array1->p_arr;
    int *p_right = array2->p_arr;
    int *p_res = malloc((array1->size + array2->size) * sizeof(int));
    int *old_p_res = p_res;

    while (p_left < array1->p_arr + array1->size && p_right < array2->p_arr + array2->size)
        *p_res++ = *p_left <= *p_right ? *p_left++ : *p_right++;
    while (p_left < array1->p_arr + array1->size)
        *p_res++ = *p_left++;
    while (p_right < array2->p_arr + array2->size)
        *p_res++ = *p_right++;

    free(array1->p_arr);
    free(array2->p_arr);

    array1->p_arr = old_p_res;
    array1->size += array2->size;
}

// merges all the arrays into arrays[0] and frees others
static void merge_arrays_list(array_struct *arrays, size_t arrays_count)
{
    if	(arrays_count <= 1)
        return;

    merge_arrays_list(arrays, arrays_count  / 2);
    merge_arrays_list(arrays + arrays_count / 2, arrays_count - arrays_count / 2);
    merge_arrays(&arrays[0], &arrays[arrays_count / 2]);
}

#define stack_size 32 * 1024 * 1024

static void* allocate_stack()
{
    void *stack = malloc(stack_size);
    stack_t ss;
    ss.ss_sp = stack;
    ss.ss_size = stack_size;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    return stack;
}

void print_coro_durations()
{
    for (int i = 0; i < files_count; ++i)
        printf("Coroutine %d ran for %ld us\n", i, coros[i].total_time * 1000000 / CLOCKS_PER_SEC);
}

void init_coros(char *filenames[], array_struct *sorted_arrays, ucontext_t *main_context)
{
    coros = malloc(files_count * sizeof(coro_struct));
    for (int i = 0; i < files_count; ++i) {
        ucontext_t *cur_context = &coros[i].context;
        getcontext(cur_context);
        cur_context->uc_stack.ss_sp = allocate_stack();
        cur_context->uc_stack.ss_size = stack_size;
        cur_context->uc_link = main_context;
        makecontext(cur_context, sort_file, 2, filenames[i], &sorted_arrays[i]);

        coros[i].last_timestamp = clock();
        coros[i].total_time = 0;
    }
}

// merges all the files into result.txt
static void sort_and_merge_files(char *filenames[])
{
    array_struct *sorted_arrays = malloc(files_count * sizeof(array_struct));

    ucontext_t main_context;
    init_coros(filenames, sorted_arrays, &main_context);
    swapcontext(&main_context, &coros[0].context);

    // by this line all coros finished their work
    print_coro_durations();

    // free coros
    for (int i = 0; i < files_count; ++i)
        free(coros[i].context.uc_stack.ss_sp);
    free(coros);

    //merge arrays into sorted_arrays[0]
    merge_arrays_list(sorted_arrays, files_count);
    array_struct res_arr = sorted_arrays[0];

    FILE *res_file = fopen("result.txt", "w");
    for (int i = 0; i < res_arr.size; ++i)
        fprintf(res_file, "%d ", res_arr.p_arr[i]);

    fclose(res_file);
    free(res_arr.p_arr);
    free(sorted_arrays);
}

int main(int argc, char** argv)
{
    assert(argc > 1);

    files_count = argc - 1;

    clock_t start_timestamp = clock();

    sort_and_merge_files(&argv[1]);

    printf("Program ran for %ld us\n", (clock() - start_timestamp) * 1000000 / CLOCKS_PER_SEC);

    return 0;
}
