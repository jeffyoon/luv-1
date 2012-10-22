#include "ray_lib.h"
#include "ray_codec.h"
#include "ray_actor.h"
#include "ray_thread.h"

static const ray_vtable_t thread_v = {
  recv : rayM_main_recv,
  send : rayM_main_send,
  close : rayM_thread_close
};

static void _thread_enter(void* arg) {
  ray_actor_t* self = (ray_actor_t*)arg;
  TRACE("ENTER: %p, L: %p, top: %i\n", self, self->L, lua_gettop(self->L));

  ray_codec_decode(self->L);

  TRACE("DECODED: %p, top: %i\n", self, lua_gettop(self->L));

  lua_remove(self->L, 1);
  luaL_checktype(self->L, 1, LUA_TFUNCTION);
  lua_pushcfunction(self->L, rayL_traceback);
  lua_insert(self->L, 1);
  int nargs = lua_gettop(self->L) - 2;

  int rv = lua_pcall(self->L, nargs, LUA_MULTRET, 1);
  lua_remove(self->L, 1); /* traceback */

  if (rv) { /* error */
    lua_pushboolean(self->L, 0);
    lua_insert(self->L, 1);
    luaL_error(self->L, lua_tostring(self->L, -1));
  }
  else {
    lua_pushboolean(self->L, 1);
    lua_insert(self->L, 1);
  }

  self->flags |= RAY_CLOSED;
}

ray_actor_t* ray_thread_new(lua_State* L) {
  int narg = lua_gettop(L);
  TRACE("narg: %i\n", narg);

  ray_actor_t* self = lua_newuserdata(L, sizeof(ray_actor_t));
  lua_State*   L1   = luaL_newstate();

  memset(self, 0, sizeof(ray_actor_t));

  self->v = thread_v;
  self->L = L1;

  ngx_queue_init(&self->queue);
  ngx_queue_init(&self->cond);

  luaL_getmetatable(L, RAY_THREAD_T);
  lua_setmetatable(L, -2);

  lua_insert(L, 1);

  luaL_openlibs(L1);

  /* keep a reference for reverse lookup in child */
  lua_pushlightuserdata(L1, (void*)self);
  lua_setfield(L1, LUA_REGISTRYINDEX, RAY_MAIN);

  /* luaopen_ray(L1); */

  lua_settop(L1, 0);
  ray_codec_encode(L, narg);
  luaL_checktype(L, -1, LUA_TSTRING);

  size_t len;
  const char* data = lua_tolstring(L, -1, &len);
  lua_pushlstring(L1, data, len);

  uv_thread_create(&self->tid, _thread_enter, self);

  /* inserted udata below function, so now just udata on top */
  lua_settop(L, 1);

  return self;
}

int rayM_thread_close(ray_actor_t* self) {
  if (!ray_is_closed(self)) {
    self->flags |= RAY_CLOSED;
    uv_close(&self->h.handle, NULL);

    uv_loop_t* loop = ray_get_loop(self->L);
    uv_loop_delete(loop);

    lua_pushnil(self->L);
    lua_setfield(self->L, LUA_REGISTRYINDEX, RAY_MAIN);
  }
  return 1;
}

/* Lua API */
static int thread_new(lua_State* L) {
  ray_actor_t* self = ray_thread_new(L);
  TRACE("new thread: %p in %p", self, L);
  return 1;
}

static int thread_join(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_THREAD_T);
  ray_actor_t* from = ray_get_self(L);

  ray_recv(from, self);

  uv_thread_join(&self->tid);

  lua_settop(from->L, 0);

  size_t len;
  int nret = lua_gettop(self->L);

  ray_codec_encode(self->L, nret);
  const char* data = lua_tolstring(self->L, -1, &len);
  lua_pushlstring(from->L, data, len);
  ray_codec_decode(from->L);

  rayM_thread_close(self);

  return nret;
}

static int thread_free(lua_State* L) {
  ray_actor_t* self = lua_touserdata(L, 1);
  TRACE("FREE: %p\n", self);
  rayM_thread_close(self);
  return 1;
}
static int thread_tostring(lua_State* L) {
  ray_actor_t* self = (ray_actor_t*)luaL_checkudata(L, 1, RAY_THREAD_T);
  lua_pushfstring(L, "userdata<%s>: %p", RAY_THREAD_T, self);
  return 1;
}

static luaL_Reg thread_funcs[] = {
  {"spawn",     thread_new},
  {NULL,        NULL}
};

static luaL_Reg thread_meths[] = {
  {"join",      thread_join},
  {"__gc",      thread_free},
  {"__tostring",thread_tostring},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray_thread(lua_State* L) {
  rayL_module(L, "ray.thread", thread_funcs);
  rayL_class (L, RAY_THREAD_T, thread_meths);
  lua_pop(L, 1);
  ray_init_main(L);
  return 1;
}
