#ifndef SESSION_H_
#define SESSION_H_

#include <asm/types.h>

struct crcdev_session {
	struct crcdev_context context;
	size_t waiting_count;
	/* context: */
	size_t scheduled_count;
	u32 poly;
	u32 sum;
};

struct crcdev_task {
	struct crcdev_session* session;
	/* block: */
	size_t data_count;
	void* cpu_addr;
	dma_addr_t dma_handle;
};

struct crcdev_slots {
};

#endif  /* SESSION_H_ */
