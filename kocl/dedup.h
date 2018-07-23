
struct fordedup {
        struct kocl_request* (*kkocl_alloc_request)(void);
        void* (*kkocl_malloc)(unsigned long nbytes,int channel);
        int (*kkocl_offload_sync)(struct kocl_request *req);
        int (*kkocl_offload_async)(struct kocl_request *req);
        int (*kkocl_next_request_id)(void);
        void (*kkocl_free_request)(struct kocl_request* req);    
        void (*kkocl_free)(void *p,int channel);
};

