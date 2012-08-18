
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
#define _DEBUG_
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


static void cb_login(GWQSession* wqs, void* ctx);
static void cb_logout(GWQSession* wqs, void* ctx);
static void cb_message_recieved(GWQSession* wqs, QQRecvMsg* msg);
static void cb_message_sent(GWQSession* wqs, QQSendMsg* msg, gint32 retCode);
void cb_users_foreach_get_buddies_count(GWQSession* wqs, GWQUserInfo* user);
static void cb_users_foreach_get_qq_num(GWQSession* wqs, GWQUserInfo* user);

typedef struct {
    GWQSession wqs;
    HybridAccount* hbAc;
    HybridGroup* hbBdGroup;
    gint32 buddiesCount;
    gint32 gotNumCount;
    gboolean accountDetailUpdated;
    gboolean accountLongNickUpdated;
}HIGWQSession;

static void HIGWQSessionShowTextMessage(HIGWQSession* hqs, QQRecvMsg* msg);
HIGWQSession* HIGWQSessionNew(const gchar* qqNum, const gchar* passwd, HybridAccount* hbAc)
{
    HIGWQSession* ret;
    
    if (!(ret = g_slice_new0(HIGWQSession))) {
        GWQ_ERR_OUT(ERR_OUT, "\n");
    }
    ret->hbAc = hbAc;
    ret->accountDetailUpdated = FALSE;
    ret->accountLongNickUpdated = FALSE;
    ret->buddiesCount = -1;
    ret->gotNumCount = 0;
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

HybridBuddy* HIGWQSessionGetBuddyByUin(HIGWQSession* hqs, gint64 uin)
{
    gchar *uId;
    GWQUserInfo *user;
    HybridBuddy *buddy = NULL;
    
    GWQ_DBG("==>%s()\n", __FUNCTION__);

    user = GWQSessionGetUserInfo(&hqs->wqs, -1, uin);
    if (!user) {
        GWQ_DBG("\n");
    }
    if (user) {
        uId = g_strdup_printf("%"G_GINT64_FORMAT, user->qqNum);
        if (uId) {
            buddy = hybrid_blist_find_buddy(hqs->hbAc, uId);
            g_free(uId);
        }
        GWQUserInfoFree(user);
    }
    return buddy;
}

void try_set_connected(HIGWQSession* hqs)
{
    GWQ_DBG("==>%s()\n", __FUNCTION__);
    if (hqs->gotNumCount == hqs->buddiesCount 
            && hqs->accountLongNickUpdated
            && hqs->accountDetailUpdated) {
        GWQ_DBG("Waiting for message\n");
        GWQSessionDoPoll(&hqs->wqs);
    }
}

void cb_users_foreach_get_buddies_count(GWQSession* wqs, GWQUserInfo* user)
{
    HIGWQSession *hqs;
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);
    
    hqs->buddiesCount++;
}

void cb_categories_foreach(GWQSession* wqs, gint32 idx, const gchar* name)
{
    HIGWQSession *hqs;
    gchar *id;
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);
    
    id = g_strdup_printf("%"G_GINT32_FORMAT, idx);
    
    GWQ_DBG("Category idx=%"G_GINT32_FORMAT"\tname=%s\n", idx, name);
    if (id){
        hybrid_blist_add_group(hqs->hbAc, id, name);
        g_free(id);
    }
}

static void cb_update_users_info(GWQSession* wqs, void* ctx)
{
    HIGWQSession *hqs;
    
    hqs = (HIGWQSession*)ctx;
    
    hqs->hbBdGroup = hybrid_blist_add_group(hqs->hbAc, BUDDIES_GROUP_ID, BUDDIES_GROUP_NAME);
    
    GWQSessionCategoriesForeach(wqs, cb_categories_foreach);
    
    hqs->buddiesCount = 0;
    GWQSessionUsersForeach(wqs, cb_users_foreach_get_buddies_count);
    
    hqs->gotNumCount = 0;
    GWQSessionUsersForeach(wqs, cb_users_foreach_get_qq_num);

}

static void cb_login(GWQSession* wqs, void* ctx)
{
    HIGWQSession *hqs;
    
	GWQ_DBG("==>__LoginCallback()\n");
    hqs = (HIGWQSession*)ctx;
	if (wqs->st != GWQS_ST_IDLE) {
		GWQ_MSG("Login failed\n");
	} else {
	    GWQ_MSG("Login successfully\n");
        GWQ_MSG("Fetching buddies' information, please wait......\n");
        
        GWQ_DBG("do hybrid_account_set_connection_status()\n");
        hybrid_account_set_connection_status(hqs->hbAc, HYBRID_CONNECTION_CONNECTED);
        
        GWQSessionUpdateUsersInfo(&hqs->wqs, cb_update_users_info);
        
        GWQSessionUpdateUserDetailedInfoByUin(&hqs->wqs, g_ascii_strtoll(wqs->num->str, NULL, 10));
        
        GWQSessionUpdateLongNickByUin(&hqs->wqs, g_ascii_strtoll(wqs->num->str, NULL, 10));
        
	}
}

static void 
cb_logout(GWQSession* wqs, void* ctx)
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
cb_message_sent(GWQSession* wqs, QQSendMsg* msg, gint32 retCode)
{
    if (retCode) {
        GWQ_ERR("Message sent failed\n");
    } else {
        GWQ_DBG("Message sent OK\n");
    }
    qq_sendmsg_free(msg);
}

static void cb_message_recieved(GWQSession* wqs, QQRecvMsg* msg)
{
    HIGWQSession *hqs;
    HybridBuddy *buddy;
    
    GWQ_DBG("==>cb_message_recieved()\n");
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);
    
    switch (msg->msg_type) {
        case MSG_STATUS_CHANGED_T:
            GWQ_DBG("form_uin=%"G_GINT64_FORMAT", uin=%"G_GINT64_FORMAT", status:%s\n", msg->from_uin, msg->uin, msg->status->str);
            buddy = HIGWQSessionGetBuddyByUin(hqs, msg->uin);
            
            if (!g_strcmp0(msg->status->str, GWQ_CHAT_ST_OFFLINE)) {
                hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_OFFLINE);
            } else if (!(g_strcmp0(msg->status->str, GWQ_CHAT_ST_HIDDEN))) {
                hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_INVISIBLE);
            } else if (!(g_strcmp0(msg->status->str, GWQ_CHAT_ST_AWAY))) {
                hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_AWAY);
            } else if (!(g_strcmp0(msg->status->str, GWQ_CHAT_ST_BUSY))) {
                hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_BUSY);
            } else if (!(g_strcmp0(msg->status->str, GWQ_CHAT_ST_ONLINE))) {
                hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_ONLINE);
            }
            break;
        case MSG_BUDDY_T:
            GWQ_DBG("[%"G_GINT64_FORMAT"]\n", msg->from_uin);
            HIGWQSessionShowTextMessage(hqs, msg);
        default:
            GWQ_DBG("Unknown message type received\n");
            break;
    }
}

static void cb_users_foreach_get_qq_num(GWQSession* wqs, GWQUserInfo* user)
{
    
    GWQ_DBG("qqNum:%"G_GINT64_FORMAT",\t nick:%s,\tmarkname:%s\n", 
                user->qqNum, user->nick ,user->markname);
    GWQSessionUpdateQQNumByUin(wqs, user->uin);
}

static void cb_update_qq_num_by_uin(GWQSession* wqs, gint64 uin, gint64 qqNum)
{
    
    HIGWQSession *hqs;
    gchar *uId, *gId;
    GWQUserInfo *user;
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);
    
    GWQ_DBG("qqNum:%"G_GINT64_FORMAT",\tuin=%"G_GINT64_FORMAT"\n", qqNum, uin);
    user = GWQSessionGetUserInfo(wqs, -1, uin);
    if (!user) {
        GWQ_MSG("ERROR\n");
    }
    if (user) {
        GWQ_DBG("category=%"G_GINT32_FORMAT",\tnick:%s,\tmarkname:%s\n", 
                user->category, user->nick, user->markname);
        uId = g_strdup_printf("%"G_GINT64_FORMAT, user->qqNum);
        if (uId) {
            HybridGroup *grp = NULL;
            const gchar *dispName = NULL;
            
            if (user->markname) {
                dispName = user->markname;
            } else if (user->nick) {
                dispName = user->nick;
            }
            
            if ((gId = g_strdup_printf("%"G_GINT32_FORMAT, user->category))) {
                grp = hybrid_blist_find_group(hqs->hbAc, gId);
                g_free(gId);
            }
            if (grp) {
				GWQ_DBG("\n");
                hybrid_blist_add_buddy(hqs->hbAc, grp, uId, dispName);
            } else {
				GWQ_DBG("\n");
                hybrid_blist_add_buddy(hqs->hbAc, hqs->hbBdGroup, uId, dispName);
            }
            g_free(uId);
        }
        GWQUserInfoFree(user);
    }
    GWQSessionUpdateLongNickByUin(wqs, uin);
    
    hqs->gotNumCount++;
    if (hqs->buddiesCount == hqs->gotNumCount) {
        GWQ_DBG("Got all qq number\n");
        GWQSessionUpdateOnlineBuddies(&hqs->wqs);
        try_set_connected(hqs);
    }
    
ERR_OUT:
    return;
}

static void cb_update_long_nick_by_uin(GWQSession*wqs, gint64 uin, const gchar* lnick)
{
    HIGWQSession *hqs;
    gchar *uId;
    GWQUserInfo *user;
    HybridBuddy *buddy = NULL;
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);

    if (uin == g_ascii_strtoll(wqs->num->str, NULL, 10)) {  /* account long nick */
        GWQ_MSG("lnick:%s\n", lnick);
        hybrid_account_set_status_text(hqs->hbAc, lnick);
        hqs->accountLongNickUpdated = TRUE;
        try_set_connected(hqs);
        return;
    }
    user = GWQSessionGetUserInfo(wqs, -1, uin);
    if (!user) {
        GWQ_DBG("\n");
    }
    if (user) {
        GWQ_DBG("qqNum=%"G_GINT64_FORMAT",\tlongNick:%s\n", 
                user->qqNum, user->lnick);
        uId = g_strdup_printf("%"G_GINT64_FORMAT, user->qqNum);
        if (uId) {
            buddy = hybrid_blist_find_buddy(hqs->hbAc, uId);
            if (buddy) {
                hybrid_blist_set_buddy_mood(buddy, user->lnick);
            }
            g_free(uId);
        }
        GWQUserInfoFree(user);
    }
}

void cb_update_user_detail_info(GWQSession* wqs, GWQUserDetailedInfo* udi)
{
    HIGWQSession *hqs;
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);
    
    if (udi->uin == g_ascii_strtoll(wqs->num->str, NULL, 10)) {
        GWQ_MSG("udi->nick: %s\n", udi->nick);
        hybrid_account_set_nickname(hqs->hbAc, udi->nick);
        hqs->accountDetailUpdated = TRUE;
        try_set_connected(hqs);
        return;
    }
}

static void cb_update_online_buddies(GWQSession* wqs, gint64 uin, const gchar* status, gint32 clientType)
{
    HIGWQSession *hqs;
    gchar *uId;
    GWQUserInfo *user;
    HybridBuddy *buddy = NULL;
    
    GWQ_DBG("==>%s()\n", __FUNCTION__);
    
    hqs = (HIGWQSession*)GWQSessionGetUserData(wqs);

    user = GWQSessionGetUserInfo(wqs, -1, uin);
    if (!user) {
        GWQ_DBG("\n");
    }
    if (user) {
        GWQ_DBG("qqNum=%"G_GINT64_FORMAT",\tstatus:%s\n", 
                user->qqNum, status);
        uId = g_strdup_printf("%"G_GINT64_FORMAT, user->qqNum);
        if (uId) {
            buddy = hybrid_blist_find_buddy(hqs->hbAc, uId);
            if (buddy) {
                if (!g_strcmp0(status, GWQ_CHAT_ST_OFFLINE)) {
                    hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_OFFLINE);
                } else if (!(g_strcmp0(status, GWQ_CHAT_ST_HIDDEN))) {
                    hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_INVISIBLE);
                } else if (!(g_strcmp0(status, GWQ_CHAT_ST_AWAY))) {
                    hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_AWAY);
                } else if (!(g_strcmp0(status, GWQ_CHAT_ST_BUSY))) {
                    hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_BUSY);
                } else if (!(g_strcmp0(status, GWQ_CHAT_ST_ONLINE))) {
                    hybrid_blist_set_buddy_state(buddy, HYBRID_STATE_ONLINE);
                }
            }
            g_free(uId);
        }
        GWQUserInfoFree(user);
    }
}

static void HIGWQSessionShowTextMessage(HIGWQSession* hqs, QQRecvMsg* msg)
{
    GString *text = g_string_new("");
    QQMsgContent *qmc;
    char *uId;
    int i;
    GWQUserInfo *ui;
    
    for (i=0; i<msg->contents->len; i++) {
        qmc = (QQMsgContent*)g_ptr_array_index(msg->contents, i);
        if (qmc) {
            if (qmc->type == QQ_MSG_CONTENT_STRING_T) {
                g_string_append(text, qmc->value.str->str);
            }
        } else {
            break;
        }
    }
    
    ui = GWQSessionGetUserInfo(&hqs->wqs, -1LL, msg->from_uin);
    if (!ui) {
        GWQ_ERR_OUT(ERR_FREE_TEXT, "\n");
    }
    uId = g_strdup_printf("%"G_GINT64_FORMAT, ui->qqNum);
    if (!uId) {
        GWQ_ERR_OUT(ERR_FREE_TEXT, "\n");
    } else {
        hybrid_conv_got_message(hqs->hbAc, uId, text->str, time(NULL));
        g_free(uId);
    }
    GWQUserInfoFree(ui);
    g_string_free(text, TRUE);
    return;
ERR_FREE_UI:
    GWQUserInfoFree(ui);
ERR_FREE_TEXT:
    g_string_free(text, TRUE);
ERR_OUT:
    return;
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
    GWQSessionSetCallBack(&hqs->wqs, 
        cb_login,
        cb_logout,
        cb_message_recieved,
        cb_message_sent,
        cb_update_qq_num_by_uin,
        cb_update_long_nick_by_uin,
        cb_update_user_detail_info,
        cb_update_online_buddies);
    GWQSessionLogin(&hqs->wqs, GWQ_CHAT_ST_HIDDEN);
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
    GWQUserInfo *user;
    HIGWQSession *hqs;
    
    hqs = hybrid_account_get_protocol_data(account);
    
    if ((user = GWQSessionGetUserInfo(&hqs->wqs, g_ascii_strtoll(buddy->id, NULL, 10), -1))) {
        char *tmpStr;
        
        hybrid_tooltip_data_add_title(tip_data, user->nick);
        tmpStr = g_strdup_printf("%"G_GINT64_FORMAT, user->qqNum);
        hybrid_tooltip_data_add_pair(tip_data, _("QQ number"), tmpStr);
        g_free(tmpStr);
        hybrid_tooltip_data_add_pair(tip_data, _("Markname"), user->markname);
        hybrid_tooltip_data_add_pair(tip_data, _("Status"), user->lnick);
        GWQUserInfoFree(user);
    }
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

static void display_sent_failed(HybridAccount *account,
                const gchar *buddy_id, const gchar *text)
{
    char *tmpStr = g_strdup_printf(_("'%s'sent failed\n"), text);
    if (tmpStr) {
        hybrid_conv_got_message(account, buddy_id, tmpStr, time(NULL));
        g_free(tmpStr);
    }
    return;
}

static void
hgwq_chat_send(HybridAccount *account, HybridBuddy *buddy, const gchar *text)
{
    HIGWQSession *hqs;
    QQSendMsg *qsm;
    QQMsgContent *qmc;
    gint64 uin, num;
    
    GWQ_DBG("==>%s()\n", __FUNCTION__);
    
    hqs = hybrid_account_get_protocol_data(account);
    
    num = g_ascii_strtoll(buddy->id, NULL, 10);
    
    if (GWQSessionGetUinByNum(&hqs->wqs, num, &uin)) {
        display_sent_failed(hqs->hbAc, buddy->id, text);
        return;
    }
    
    qsm = qq_sendmsg_new(MSG_BUDDY_T,  num);  /* qsm should be freed with qq_sendmsg_free!! */

    qmc = qq_msgcontent_new(QQ_MSG_CONTENT_STRING_T, text);
    qq_sendmsg_add_content(qsm, qmc);
    qmc = qq_msgcontent_new(QQ_MSG_CONTENT_FONT_T, "宋体", 12, "000000", 0,0,0);
    qq_sendmsg_add_content(qsm, qmc);
    
    GWQ_DBG("do GWQSessionSendBuddyMsg()\n");
    if (GWQSessionSendBuddyMsg(&hqs->wqs,  uin, qsm)) {
        GWQ_ERR("Sent failed, BUSY sending message, please try later\n");
        display_sent_failed(hqs->hbAc, buddy->id, text);
        qq_sendmsg_free(qsm);
    };
}

static void
hgwq_close(HybridAccount *account)
{
    HIGWQSession *hqs;
    
    hqs = hybrid_account_get_protocol_data(account);
    GWQSessionLogOut(&hqs->wqs);
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
