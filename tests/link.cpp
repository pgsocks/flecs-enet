#include <flecs_enet.h>
int main()
{
	flecs_enet_host_config_t config{};
	ecs_world_t *world = ecs_init();
	flecs_enet_ids_t ids{};
	flecs_enet_result_t result = flecs_enet_import(world, &ids);
	ecs_fini(world);
	return !config.listen && result == FLECS_ENET_OK ? 0 : 1;
}
