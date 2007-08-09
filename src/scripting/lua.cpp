/*
 *  The Mana World Server
 *  Copyright 2007 The Mana World Development Team
 *
 *  This file is part of The Mana World.
 *
 *  The Mana World is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  The Mana World is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with The Mana World; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  $Id$
 */

#include <cassert>

extern "C" {
#include <lualib.h>
#include <lauxlib.h>
}

#include "defines.h"
#include "resourcemanager.h"
#include "game-server/character.hpp"
#include "game-server/gamehandler.hpp"
#include "game-server/npc.hpp"
#include "net/messageout.hpp"
#include "scripting/script.hpp"
#include "utils/logger.h"

/**
 * Implementation of the Script class for Lua.
 */
class LuaScript: public Script
{
    public:

        LuaScript(lua_State *);

        ~LuaScript();

        void prepare(std::string const &);

        void push(int);

        void push(Thing *);

        int execute();

    private:

        lua_State *mState;
        int nbArgs;
};

/* Functions below are unsafe, as they assume the script has passed pointers
   to objects which have not yet been destroyed. If the script never keeps
   pointers around, there will be no problem. In order to be safe, the engine
   should replace pointers by local identifiers and store them in a map. By
   listening to the death of objects, it could keep track of pointers still
   valid in the map.
   TODO: do it. */

static NPC *getNPC(lua_State *s, int p)
{
    if (!lua_islightuserdata(s, p)) return NULL;
    Thing *t = static_cast<Thing *>(lua_touserdata(s, p));
    if (t->getType() != OBJECT_NPC) return NULL;
    return static_cast<NPC *>(t);
}

static Character *getCharacter(lua_State *s, int p)
{
    if (!lua_islightuserdata(s, p)) return NULL;
    Thing *t = static_cast<Thing *>(lua_touserdata(s, p));
    if (t->getType() != OBJECT_CHARACTER) return NULL;
    return static_cast<Character *>(t);
}

/**
 * Callback for sending a NPC_MESSAGE (1: NPC, 2: Character, 3: string).
 */
static int LuaMsg_NpcMessage(lua_State *s)
{
    NPC *p = getNPC(s, 1);
    Character *q = getCharacter(s, 2);
    size_t l;
    char const *m = lua_tolstring(s, 3, &l);
    if (!p || !q || !m)
    {
        LOG_WARN("LuaMsg_NpcMessage called with incorrect parameters.");
        return 0;
    }
    MessageOut msg(GPMSG_NPC_MESSAGE);
    msg.writeShort(p->getPublicID());
    msg.writeString(std::string(m), l);
    gameHandler->sendTo(q, msg);
    return 0;
}

/**
 * Callback for sending a NPC_CHOICE (1: NPC, 2: Character, 3: string).
 */
static int LuaMsg_NpcChoice(lua_State *s)
{
    NPC *p = getNPC(s, 1);
    Character *q = getCharacter(s, 2);
    size_t l;
    char const *m = lua_tolstring(s, 3, &l);
    if (!p || !q || !m)
    {
        LOG_WARN("LuaMsg_NpcChoice called with incorrect parameters.");
        return 0;
    }
    MessageOut msg(GPMSG_NPC_CHOICE);
    msg.writeShort(p->getPublicID());
    msg.writeString(std::string(m), l);
    gameHandler->sendTo(q, msg);
    return 0;
}

LuaScript::LuaScript(lua_State *s):
    mState(s),
    nbArgs(-1)
{
    luaL_openlibs(mState);
    // A Lua state is like a function, so "execute" it in order to initialize it.
    int res = lua_pcall(mState, 0, 0, 0);
    if (res)
    {
        LOG_ERROR("Failure while initializing Lua script: "
                  << lua_tostring(mState, 1));
        lua_pop(mState, 1);
        return;
    }

    static luaL_reg const callbacks[] = {
        { "msg_npc_message", &LuaMsg_NpcMessage },
        { "msg_npc_choice",  &LuaMsg_NpcChoice  },
        { NULL, NULL }
    };
    luaL_register(mState, "tmw", callbacks);
    lua_pop(mState, 1);
}

LuaScript::~LuaScript()
{
    lua_close(mState);
}

void LuaScript::prepare(std::string const &name)
{
    assert(nbArgs == -1);
    lua_getglobal(mState, name.c_str());
    nbArgs = 0;
}

void LuaScript::push(int v)
{
    assert(nbArgs >= 0);
    lua_pushinteger(mState, v);
    ++nbArgs;
}

void LuaScript::push(Thing *v)
{
    assert(nbArgs >= 0);
    lua_pushlightuserdata(mState, v);
    ++nbArgs;
}

int LuaScript::execute()
{
    assert(nbArgs >= 0);
    int res = lua_pcall(mState, nbArgs, 1, 0);
    nbArgs = -1;
    if (res || !lua_isnumber(mState, 1))
    {
        char const *s = lua_tostring(mState, 1);
        LOG_WARN("Failure while calling Lua function: error=" << res
                 << ", type=" << lua_typename(mState, lua_type(mState, 1))
                 << ", message=" << (s ? s : ""));
        lua_pop(mState, 1);
        return 0;
    }
    res = lua_tointeger(mState, 1);
    lua_pop(mState, 1);
    return res;
}

static Script *loadScript(std::string const &filename)
{
    // Load the file through resource manager.
    ResourceManager *resman = ResourceManager::getInstance();
    int fileSize;
    char *buffer = (char *)resman->loadFile(filename, fileSize);
    if (!buffer) return NULL;

    lua_State *s = luaL_newstate();
    int res = luaL_loadstring(s, buffer);
    free(buffer);

    switch(res)
    {
        case 0:
            LOG_INFO("Successfully loaded script " << filename);
            return new LuaScript(s);
        case LUA_ERRSYNTAX:
            LOG_ERROR("Syntax error while loading script " << filename);
    }

    lua_close(s);
    return NULL;
}

struct LuaRegister
{
    LuaRegister() { Script::registerEngine("lua", loadScript); }
};

static LuaRegister dummy;


