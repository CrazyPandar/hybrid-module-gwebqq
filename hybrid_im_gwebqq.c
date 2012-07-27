
/***************************************************************************
 *   Copyright (C) 2011 by CrazyPandar                                           *
 *   CrazyPandar@gmail.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.            *
 ***************************************************************************/

#include <glib.h>
#include <g_webqq.h>
#include "util.h"
#include "eventloop.h"
#include "account.h"
#include "module.h"
#include "info.h"
#include "blist.h"
#include "notify.h"
#include "action.h"
#include "conv.h"
#include "gtkutils.h"
#include "tooltip.h"

#define BUDDIES_GROUP_ID    "buddies"
#define BUDDIES_GROUP_NAME    "Buddies"

typedef struct {
    GWQSession wqs;
    HybridAccount* hbAc;
    HybridGroup* hbBdGroup;
}HIGWQSession;

HIGWQSession* HIGWQSessionNew(const gchar* qqNum, const gchar* passwd, HybridAccount* hbAc)
{
    HIGWQSession* ret;
    
    if (!(ret = g_slice_new0(HIGWQSession))) {
        GWQ_ERR_OUT(ERR_OUT, "\n");
    }
    ret->hbAc = hbAc;
    
    if (GWQSessionInit(&ret->wqs, qqNum, passwd, ret)) {
        GWQ_ERR_OUT(ERR_FREE_RET, "\n");
    }
    
    return ret;
ERR_EXIT_WQS:
    GWQSessionExit(&ret->wqs);
ERR_FREE_RET:
    g_slice_free(HIGWQSession, ret);
ERR_OUT:
    return NULL;
}

void HIGWQSessionDestroy(HIGWQSession* hqs)
{
    GWQSessionExit(&hqs->wqs);
    g_slice_free(HIGWQSession, hqs);
}

static void _DisplayMsg(gpointer data, gpointer user_data)
{
    QQMsgContent *qmc;
    
    qmc = (QQMsgContent*)data;
    
    if (qmc->type == QQ_MSG_CONTENT_STRING_T) {
        GWQ_MSG("%s\n", qmc->value.str->str);
    }
}

static void messageRecieved(GWQSession* wqs, QQRecvMsg* msg)
{
    GWQ_DBG("==>messageRecieved()\n");
    switch (msg->msg_type) {
        case MSG_STATUS_CHANGED_T:
            GWQ_MSG("uin=%"G_GINT64_FORMAT", status:%s\n", msg->uin, msg->status->str);
            break;
        case MSG_BUDDY_T:
            GWQ_MSG("[%"G_GINT64_FORMAT"]\n", msg->from_uin);
            g_ptr_array_foreach(msg->contents, _DisplayMsg, wqs);
        default:
            GWQ_MSG("Unknown message type received\n");
            break;
    }
}

static void cb_users_foreach(GWQSession* wqs, GWQUserInfo* user)
{
    HybridGroup* bGroup;
    HIGWQSession *hqs;
    char *uId;
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);
    
    GWQ_MSG("qqNum:%"G_GINT64_FORMAT",\t nick:%s,\t markname:%s\n", 
            user->qqNum, user->nick->str ,user->markname->str);
    bGroup = hqs->hbBdGroup;
    if (!bGroup) {
        GWQ_ERR("Can not find default buddies group\n");
    }
    
    uId = g_strdup_printf("%"G_GINT64_FORMAT, user->uin);
    if (!uId) {
        GWQ_ERR("\n");
    } else {
        hybrid_blist_add_buddy(hqs->hbAc, hqs->hbBdGroup, uId,
            user->nick->str);
        g_free(uId);
    }
}

static void cb_update_users_info(GWQSession* wqs, void* ctx)
{
	GWQ_DBG("==>__LoginCallback()\n");

    GWQ_MSG("=== Buddies List ===\n");
    GWQSessionUsersForeach(wqs, cb_users_foreach);
    GWQ_MSG("====================\n");
    
    GWQ_DBG("Waiting for message\n");
    GWQSessionDoPoll(wqs, messageRecieved);
    
}

static void cb_login(GWQSession* wqs, void* ctx)
{
    HIGWQSession *hqs;
    GWQUserInfo *ui;
    
	GWQ_DBG("==>__LoginCallback()\n");
    hqs = (HIGWQSession*)ctx;
	if (wqs->st != GWQS_ST_IDLE) {
		GWQ_MSG("Login failed\n");
	} else {
	    GWQ_MSG("Login successfully\n");
        GWQ_MSG("Fetching buddies' information, please wait......\n");
        
        GWQSessionUpdateUsersInfo(&hqs->wqs, cb_update_users_info);
        
        ui = GWQSessionGetUserInfo(&hqs->wqs, g_ascii_strtoll(hqs->wqs.num->str, NULL, 10), -1);
        if (ui) {
            hybrid_account_set_nickname(hqs->hbAc, ui->nick->str);
        
            GWQUserInfoFree(ui);
        }
        
        hybrid_account_set_connection_status(hqs->hbAc, HYBRID_CONNECTION_CONNECTED);
        
        hqs->hbBdGroup = hybrid_blist_add_group(hqs->hbAc, BUDDIES_GROUP_ID, BUDDIES_GROUP_NAME);
        if (!hqs->hbBdGroup) {
            GWQ_ERR("\n");
        }
	}
}

static gboolean
hgwq_login(HybridAccount *account)
{
    HIGWQSession* hqs;
    
    hqs = HIGWQSessionNew(account->username,
                             account->password, account);
    hybrid_account_set_protocol_data(account, hqs);
    hybrid_account_set_state(hqs->hbAc, HYBRID_STATE_INVISIBLE);
    hybrid_account_set_nickname(hqs->hbAc, account->username);
    GWQSessionLogin(&hqs->wqs, cb_login, GWQ_CHAT_ST_HIDDEN);
    return FALSE;
}


static void
hgwq_get_info(HybridAccount *account, HybridBuddy *buddy)
{

}

static gboolean
hgwq_modify_name(HybridAccount *account, const gchar *name)
{

    return TRUE;
}

static gboolean
hgwq_modify_status(HybridAccount *account, const gchar *status)
{


    return TRUE;
}

static gboolean
hgwq_modify_photo(HybridAccount *account, const gchar *filename)
{


    return TRUE;
}

static gboolean
hgwq_change_state(HybridAccount *account, gint state)
{

    HIGWQSession *hqs;

    hqs = hybrid_account_get_protocol_data(account);


    return TRUE;
}

static gboolean
hgwq_keep_alive(HybridAccount *account)
{


    return TRUE;
}

static gboolean
hgwq_account_tooltip(HybridAccount *account, HybridTooltipData *tip_data)
{

    return TRUE;
}

static gboolean
hgwq_buddy_tooltip(HybridAccount *account, HybridBuddy *buddy,
        HybridTooltipData *tip_data)
{
   
    return TRUE;
}

static gboolean
hgwq_buddy_remove(HybridAccount *account, HybridBuddy *buddy)
{
   
    return TRUE;
}

static gboolean
hgwq_buddy_rename(HybridAccount *account, HybridBuddy *buddy, const gchar *text)
{
    return TRUE;
}

static gboolean
hgwq_buddy_add(HybridAccount *account, HybridGroup *group, const gchar *name,
               const gchar *alias, const gchar *tips)
{

    return TRUE;
}

static gboolean
hgwq_buddy_req(HybridAccount *account, HybridGroup *group,
               const gchar *id, const gchar *alias,
               gboolean accept, const gpointer user_data)
{
    return FALSE;
}

static void
hgwq_group_add(HybridAccount *account, const gchar *text)
{
    hybrid_blist_add_group(account, text, text);
}

static gboolean
hgwq_buddy_move(HybridAccount *account, HybridBuddy *buddy,
                HybridGroup *new_group)
{

    return TRUE;
}

static void
hgwq_send_typing(HybridAccount *account, HybridBuddy *buddy, HybridInputState state)
{

}

static void
hgwq_chat_send(HybridAccount *account, HybridBuddy *buddy, const gchar *text)
{

}

static void cb_logout(GWQSession* wqs, void* ctx)
{
    HIGWQSession *hqs;
    
    hqs = (HIGWQSession*)ctx;
    if (wqs->st != GWQS_ST_OFFLINE) {
		GWQ_MSG("Logout failed\n");
	} else {
	    GWQ_MSG("Logout success\n");
	}
    HIGWQSessionDestroy(hqs);
}

static void
hgwq_close(HybridAccount *account)
{
    HIGWQSession *hqs;
    
    hqs = hybrid_account_get_protocol_data(account);
    GWQSessionLogOut(&hqs->wqs, cb_logout);
}

HybridIMOps im_ops = {
    hgwq_login,                 /**< login */
    hgwq_get_info,              /**< get_info */
    hgwq_modify_name,           /**< modify_name */
    hgwq_modify_status,         /**< modify_status */
    hgwq_modify_photo,          /**< modify_photo */
    hgwq_change_state,          /**< change_state */
    hgwq_keep_alive,            /**< keep_alive */
    hgwq_account_tooltip,       /**< account_tooltip */
    hgwq_buddy_tooltip,         /**< buddy_tooltip */
    hgwq_buddy_move,            /**< buddy_move */
    hgwq_buddy_remove,          /**< buddy_remove */
    hgwq_buddy_rename,          /**< buddy_rename */
    hgwq_buddy_add,             /**< buddy_add */
    hgwq_buddy_req,             /**< buddy_req */
    NULL,                       /**< group_rename */
    NULL,                       /**< group_remove */
    hgwq_group_add,             /**< group_add */
    NULL,                       /**< chat_word_limit */
    NULL,                       /**< chat_start */
    hgwq_send_typing,           /**< chat_send_typing */
    hgwq_chat_send,             /**< chat_send */
    hgwq_close,                 /**< close */
};

HybridModuleInfo module_info = {
    "gwebqq",                     /**< name */
    "CrazyPandar@gmail.com",                 /**< author */
    N_("webqq client"),        /**< summary */
    /* description */
    N_("implement webqq protocol"),
    "https://github.com/CrazyPandar/libgwebqq",      /**< homepage */
    "0","1",                    /**< major version, minor version */
    "xmpp",                     /**< icon name */
    MODULE_TYPE_IM,

    &im_ops,
    NULL,
    NULL,
    NULL, /**< actions */
};

void
hgwq_module_init(HybridModule *module)
{

}

HYBRID_MODULE_INIT(hgwq_module_init, &module_info);
