#ifndef _RAY_TIMER_H_
#define _RAY_TIMER_H_

#include "ray_lib.h"

int rayM_timer_send (ray_actor_t* self, ray_actor_t* from, int narg);
int rayM_timer_recv (ray_actor_t* self, ray_actor_t* that);
int rayM_timer_close (ray_actor_t* self);

LUALIB_API int luaopen_ray_timer(lua_State* L);

#endif /* _RAY_TIMER_H_ */