#include <flecs_enet.h>
int main(void)
{
	ecs_world_t *world = ecs_init();
	flecs_enet_ids_t ids;
	flecs_enet_result_t result = flecs_enet_import(world, &ids);
	ecs_fini(world);
	return result == FLECS_ENET_OK ? 0 : 1;
}
