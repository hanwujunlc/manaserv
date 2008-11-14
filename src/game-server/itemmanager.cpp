/*
 *  The Mana World
 *  Copyright 2004 The Mana World Development Team
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

#include <map>
#include <set>

#include "game-server/itemmanager.hpp"

#include "defines.h"
#include "game-server/attackzone.hpp"
#include "game-server/item.hpp"
#include "game-server/resourcemanager.hpp"
#include "scripting/script.hpp"
#include "utils/logger.h"
#include "utils/xml.hpp"

#include <sstream>

typedef std::map< int, ItemClass * > ItemClasses;
static ItemClasses itemClasses; /**< Item reference */
static std::string itemReferenceFile;
static unsigned int itemDatabaseVersion = 0; /**< Version of the loaded items database file.*/

void ItemManager::initialize(std::string const &file)
{
    itemReferenceFile = file;
    reload();
}

void ItemManager::reload()
{
    int size;
    char *data = ResourceManager::loadFile(itemReferenceFile, size);

    if (!data) {
        LOG_ERROR("Item Manager: Could not find " << itemReferenceFile << "!");
        free(data);
        return;
    }

    xmlDocPtr doc = xmlParseMemory(data, size);
    free(data);

    if (!doc)
    {
        LOG_ERROR("Item Manager: Error while parsing item database ("
                  << itemReferenceFile << ")!");
        return;
    }

    xmlNodePtr node = xmlDocGetRootElement(doc);
    if (!node || !xmlStrEqual(node->name, BAD_CAST "items"))
    {
        LOG_ERROR("Item Manager: " << itemReferenceFile
                  << " is not a valid database file!");
        xmlFreeDoc(doc);
        return;
    }

    LOG_INFO("Loading item reference...");
    unsigned nbItems = 0;
    for (node = node->xmlChildrenNode; node != NULL; node = node->next)
    {
        // Try to load the version of the item database. The version is defined
        // as subversion tag embedded as XML attribute. So every modification
        // to the items.xml file will increase the revision automatically.
        if (xmlStrEqual(node->name, BAD_CAST "version"))
        {
            std::string revision = XML::getProperty(node, "revision", std::string());
            itemDatabaseVersion = atoi(revision.c_str());

            LOG_INFO("Loading item database version " << itemDatabaseVersion);
            continue;
        }

        if (!xmlStrEqual(node->name, BAD_CAST "item"))
        {
            continue;
        }

        int id = XML::getProperty(node, "id", 0);
        if (id == 0)
        {
            LOG_WARN("Item Manager: An (ignored) item has no ID in "
                     << itemReferenceFile << "!");
            continue;
        }

        std::string sItemType = XML::getProperty(node, "type", "");
        ItemType itemType = itemTypeFromString(sItemType);

        if (itemType == ITEM_UNKNOWN)
        {
            LOG_WARN(itemReferenceFile<<": Unknown item type \""<<sItemType
                     <<"\" for item #"<<id<<" - treating it as \"generic\"");
            itemType = ITEM_UNUSABLE;
        }

        if (itemType == ITEM_HAIRSPRITE || itemType == ITEM_RACESPRITE)
        {
            continue;
        }

        ItemClass *item;
        ItemClasses::iterator i = itemClasses.find(id);
        if (i == itemClasses.end())
        {
            item = new ItemClass(id, itemType);
            itemClasses[id] = item;
        }
        else
        {
            item = i->second;
        }

        int weight = XML::getProperty(node, "weight", 0);
        int value = XML::getProperty(node, "value", 0);
        int maxPerSlot = XML::getProperty(node, "max-per-slot", 0);
        int sprite = XML::getProperty(node, "sprite_id", 0);
        std::string scriptName = XML::getProperty(node, "script_name", std::string());
        std::string attackShape = XML::getProperty(node, "attack-shape", "cone");
        std::string attackTarget = XML::getProperty(node, "attack-target", "multi");
        int attackRange = XML::getProperty(node, "attack-range", 32);
        int attackAngle = XML::getProperty(node, "attack-angle", 90);

        ItemModifiers modifiers;
        if (itemType == ITEM_EQUIPMENT_ONE_HAND_WEAPON ||
            itemType == ITEM_EQUIPMENT_TWO_HANDS_WEAPON)
        {
            std::string sWeaponType = XML::getProperty(node, "weapon-type", "");
            WeaponType weaponType = weaponTypeFromString(sWeaponType);
            if (weaponType == WPNTYPE_NONE)
            {
                LOG_WARN(itemReferenceFile<<": Unknown weapon type \""
                         <<sWeaponType<<"\" for item #"<<id<<" - treating it as generic item");
                itemType = ITEM_UNUSABLE;
            }
            modifiers.setValue(MOD_WEAPON_TYPE, weaponType);
            modifiers.setValue(MOD_WEAPON_RANGE,  XML::getProperty(node, "range",       0));
            modifiers.setValue(MOD_ELEMENT_TYPE,  XML::getProperty(node, "element",     0));
        }
        modifiers.setValue(MOD_LIFETIME,      XML::getProperty(node, "lifetime", 0) * 10);
        //TODO: add child nodes for these modifiers (additive and factor)
        modifiers.setAttributeValue(BASE_ATTR_PHY_ATK_MIN,      XML::getProperty(node, "attack-min",      0));
        modifiers.setAttributeValue(BASE_ATTR_PHY_ATK_DELTA,      XML::getProperty(node, "attack-delta",      0));
        modifiers.setAttributeValue(BASE_ATTR_HP,      XML::getProperty(node, "hp",      0));
        modifiers.setAttributeValue(BASE_ATTR_PHY_RES, XML::getProperty(node, "defense", 0));
        modifiers.setAttributeValue(CHAR_ATTR_STRENGTH,     XML::getProperty(node, "strength",     0));
        modifiers.setAttributeValue(CHAR_ATTR_AGILITY,      XML::getProperty(node, "agility",      0));
        modifiers.setAttributeValue(CHAR_ATTR_DEXTERITY,    XML::getProperty(node, "dexterity",    0));
        modifiers.setAttributeValue(CHAR_ATTR_VITALITY,     XML::getProperty(node, "vitality",     0));
        modifiers.setAttributeValue(CHAR_ATTR_INTELLIGENCE, XML::getProperty(node, "intelligence", 0));
        modifiers.setAttributeValue(CHAR_ATTR_WILLPOWER,    XML::getProperty(node, "willpower",    0));

        if (maxPerSlot == 0)
        {
            LOG_WARN("Item Manager: Missing max-per-slot property for "
                     "item " << id << " in " << itemReferenceFile << '.');
            maxPerSlot = 1;
        }

        if (itemType > ITEM_USABLE && itemType < ITEM_EQUIPMENT_AMMO &&
            maxPerSlot != 1)
        {
            LOG_WARN("Item Manager: Setting max-per-slot property to 1 for "
                     "equipment " << id << " in " << itemReferenceFile << '.');
            maxPerSlot = 1;
        }

        if (weight == 0)
        {
            LOG_WARN("Item Manager: Missing weight for item "
                     << id << " in " << itemReferenceFile << '.');
            weight = 1;
        }

        // TODO: Clean this up some
        Script *s = 0;
        std::stringstream filename;
        filename << "scripts/items/" << id << ".lua";

        if (ResourceManager::exists(filename.str()))       // file exists!
        {
            LOG_INFO("Loading item script: " + filename.str());
            s = Script::create("lua");
            s->loadFile(filename.str());
        }

        item->setWeight(weight);
        item->setCost(value);
        item->setMaxPerSlot(maxPerSlot);
        item->setScript(s);
        item->setModifiers(modifiers);
        item->setSpriteID(sprite ? sprite : id);
        ++nbItems;

        if (itemType == ITEM_EQUIPMENT_ONE_HAND_WEAPON ||
            itemType == ITEM_EQUIPMENT_TWO_HANDS_WEAPON)
        {
            AttackZone *zone = new AttackZone;

            if (attackShape == "cone")
            {
                zone->shape = ATTZONESHAPE_CONE;
            }
            else
            {
                LOG_WARN("Item Manager: Unknown attack zone shape \"" << attackShape
                         <<"\" for weapon " << id << " in " << itemReferenceFile << '.');
                zone->shape = ATTZONESHAPE_CONE;
            }

            if (attackTarget == "multi")
            {
                zone->multiTarget = true;
            }
            else if (attackTarget == "single")
            {
                zone->multiTarget = false;
            }
            else
            {
                LOG_WARN("Item Manager: Unknown target mode \"" << attackTarget
                         <<"\" for weapon " << id << " in " << itemReferenceFile << '.');
                zone->multiTarget = true;
            }
            zone->range = attackRange;
            zone->angle = attackAngle;

            item->setAttackZone(zone);
        }

        LOG_DEBUG("Item: ID: " << id << ", itemType: " << itemType
                  << ", weight: " << weight << ", value: " << value <<
                  ", scriptName: " << scriptName << ", maxPerSlot: " << maxPerSlot << ".");
    }

    LOG_INFO("Loaded " << nbItems << " items from "
             << itemReferenceFile << ".");

    xmlFreeDoc(doc);
}

void ItemManager::deinitialize()
{
    for (ItemClasses::iterator i = itemClasses.begin(), i_end = itemClasses.end(); i != i_end; ++i)
    {
        delete i->second;
    }
    itemClasses.clear();
}

ItemClass *ItemManager::getItem(int itemId)
{
    ItemClasses::const_iterator i = itemClasses.find(itemId);
    return i != itemClasses.end() ? i->second : NULL;
}

unsigned int ItemManager::GetDatabaseVersion(void)
{
    return itemDatabaseVersion;
}
