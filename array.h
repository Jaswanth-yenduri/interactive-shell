#ifndef ISH_ARRAY_H_
#define ISH_ARRAY_H_

typedef struct array {
        int len;
        int cap;
        void **buf;        
} array_t;

extern array_t *array_new(void);
extern void array_free(array_t *);
extern void *array_get(array_t *, int);
extern void array_append(array_t *, void *);

#endif  /* ISH_ARRAY_H_ */
