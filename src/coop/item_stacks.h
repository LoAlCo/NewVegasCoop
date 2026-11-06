// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

namespace coop
{
    struct StackNode
    {
        const void* stack;
        const StackNode* next;
    };

    struct ItemNode
    {
        void* item;
        ItemNode* next;
    };

    // The game's lists keep their first node inside whatever owns the list, so a node is emptied or takes over the next
    // one's contents, never freed: the few bytes of a node taken out of the middle stay with the game until it closes.
    inline bool RemoveFromList(ItemNode* head, const void* item)
    {
        int seen = 0;
        ItemNode* before = nullptr;
        for (ItemNode* node = head; node && ++seen <= 10000; before = node, node = node->next)
        {
            if (node->item != item)
                continue;
            if (node->next)
            {
                node->item = node->next->item;
                node->next = node->next->next;
            }
            else if (before)
                before->next = nullptr;
            else
                node->item = nullptr;
            return true;
        }
        return false;
    }

    inline bool HoldsStack(const StackNode* head, const void* stack)
    {
        int seen = 0;
        for (const StackNode* node = head; node; node = node->next)
            if (node->stack == stack || ++seen > 10000)
                return true;
        return false;
    }
}
