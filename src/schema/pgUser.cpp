//////////////////////////////////////////////////////////////////////////
//
// pgAdmin III - PostgreSQL Tools
// RCS-ID:      $Id$
// Copyright (C) 2002 - 2004, The pgAdmin Development Team
// This software is released under the Artistic Licence
//
// pgUser.cpp - PostgreSQL User
//
//////////////////////////////////////////////////////////////////////////

// wxWindows headers
#include <wx/wx.h>

// App headers
#include "pgAdmin3.h"
#include "misc.h"
#include "pgObject.h"
#include "pgUser.h"
#include "pgCollection.h"
#include "frmMain.h"
#include "pgDefs.h"

pgUser::pgUser(const wxString& newName)
: pgServerObject(PG_USER, newName)
{
    wxLogInfo(wxT("Creating a pgUser object"));
}

pgUser::~pgUser()
{
    wxLogInfo(wxT("Destroying a pgUser object"));
}



bool pgUser::DropObject(wxFrame *frame, wxTreeCtrl *browser)
{
    if (GetUpdateCatalog())
    {
        wxMessageDialog dlg(frame, 
            _("Deleting a superuser might result in unwanted behaviour (e.g. when restoring the database).\nAre you sure?"),
            _("Confirm superuser deletion"),
                     wxICON_EXCLAMATION | wxYES_NO |wxNO_DEFAULT);
        if (dlg.ShowModal() != wxID_YES)
            return false;
    }
    return server->ExecuteVoid(wxT("DROP USER ") + GetQuotedFullIdentifier() + wxT(";"));
}


wxString pgUser::GetSql(wxTreeCtrl *browser)
{
    if (sql.IsNull())
    {
        sql = wxT("-- User: \"") + GetName() + wxT("\"\n\n")
            + wxT("-- DROP USER ") + GetQuotedFullIdentifier() + wxT(";")
            + wxT("\n\nCREATE USER ") + GetQuotedIdentifier()
            + wxT("\n  WITH SYSID ") + NumToStr(userId);
        if (GetPassword() != wxT("********"))
            AppendIfFilled(sql, wxT("\n  PASSWORD ENCRYPTED "), GetPassword());
        sql += wxT("\n ");
        if (GetCreateDatabase())    sql += wxT(" CREATEDB");
        else                        sql += wxT(" NOCREATEDB");
        if (GetUpdateCatalog())     sql += wxT(" CREATEUSER");
        else                        sql += wxT(" NOCREATEUSER");
        if (GetAccountExpires().IsValid())
        AppendIfFilled(sql, wxT(" VALID UNTIL "), qtString(DateToAnsiStr(GetAccountExpires())));
        sql +=wxT(";\n");

        size_t index;
        for (index=0 ; index < configList.GetCount() ; index++)
        {
            sql += wxT("ALTER USER ") + GetQuotedIdentifier()
                + wxT(" SET ") + configList.Item(index) + wxT(";\n");
        }
        for (index=0 ; index < groupsIn.GetCount() ; index++)
            sql += wxT("ALTER GROUP ") + qtIdent(groupsIn.Item(index))
                +  wxT(" ADD USER ") + GetQuotedIdentifier() + wxT(";\n");

    }
    return sql;
}



void pgUser::ShowReferencedBy(frmMain *form, ctlListView *referencedBy, const wxString &where)
{
    form->StartMsg(_(" Retrieving user owned objects"));

    referencedBy->ClearAll();
    referencedBy->AddColumn(_("Type"), 60);
    referencedBy->AddColumn(_("Database"), 80);
    referencedBy->AddColumn(_("Name"), 300);

    wxString uid=NumToStr(GetUserId());
    wxString sysoid = NumToStr(GetConnection()->GetLastSystemOID());

    wxArrayString dblist;

    pgSet *set;
    if (GetConnection()->BackendMinimumVersion(7, 5))
        set=GetConnection()->ExecuteSet(
        wxT("SELECT 'd' as type, datname, datallowconn, datdba\n")
        wxT("  FROM pg_database db\n")
        wxT("UNION\n")
        wxT("SELECT 'M', spcname, null, null\n")
        wxT("  FROM pg_tablespace where spcowner=") + uid + wxT("\n")
        wxT(" ORDER BY 1, 2"));
    else
        set=GetConnection()->ExecuteSet(
        wxT("SELECT 'd' as type, datname, datallowconn, datdba\n")
        wxT("  FROM pg_database db"));

    if (set)
    {
        while (!set->Eof())
        {
            wxString name=set->GetVal(wxT("datname"));
            if (set->GetVal(wxT("type")) == wxT("d"))
            {
                if (set->GetBool(wxT("datallowconn")))
                    dblist.Add(name);
                if (GetUserId() == set->GetLong(wxT("datdba")))
                    referencedBy->AppendItem(PGICON_DATABASE, _("Database"), name);
            }
            else
                referencedBy->AppendItem(PGICON_TABLESPACE, _("Tablespace"), wxEmptyString, name);

            set->MoveNext();
        }
        delete set;
    }

    FillOwned(form->GetBrowser(), referencedBy, dblist, 
        wxT("SELECT cl.relkind, COALESCE(cin.nspname, cln.nspname) as nspname, COALESCE(ci.relname, cl.relname) as relname, ci.relname as indname\n")
        wxT("  FROM pg_class cl\n")
        wxT("  JOIN pg_namespace cln ON cl.relnamespace=cln.oid\n")
        wxT("  LEFT OUTER JOIN pg_index ind ON ind.indexrelid=cl.oid\n")
        wxT("  LEFT OUTER JOIN pg_class ci ON ind.indrelid=ci.oid\n")
        wxT("  JOIN pg_namespace cin ON ci.relnamespace=cin.oid\n")
        wxT(" WHERE cl.relowner = ") + uid + wxT(" AND cl.oid > ") + sysoid + wxT("\n")
        wxT("UNION ALL\n")
        wxT("SELECT 'n', null, nspname, null\n")
        wxT("  FROM pg_namespace nsp WHERE nspowner = ") + uid + wxT(" AND nsp.oid > ") + sysoid + wxT("\n")
        wxT("UNION ALL\n")
        wxT("SELECT CASE WHEN typtype='d' THEN 'd' ELSE 'y' END, null, typname, null\n")
        wxT("  FROM pg_type ty WHERE typowner = ") + uid + wxT(" AND ty.oid > ") + sysoid + wxT("\n")
        wxT("UNION ALL\n")
        wxT("SELECT 'C', null, conname, null\n")
        wxT("  FROM pg_conversion co WHERE conowner = ") + uid + wxT(" AND co.oid > ") + sysoid + wxT("\n")
        wxT("UNION ALL\n")
        wxT("SELECT CASE WHEN prorettype=") + NumToStr(PGOID_TYPE_TRIGGER) + wxT(" THEN 'T' ELSE 'p' END, null, proname, null\n")
        wxT("  FROM pg_proc pr WHERE proowner = ") + uid + wxT(" AND pr.oid > ") + sysoid + wxT("\n")
        wxT("UNION ALL\n")
        wxT("SELECT 'o', null, oprname || '('::text || ")
                    wxT("COALESCE(tl.typname, ''::text) || ")
                    wxT("CASE WHEN tl.oid IS NOT NULL AND tr.oid IS NOT NULL THEN ','::text END || ")
                    wxT("COALESCE(tr.typname, ''::text) || ')'::text, null\n")
        wxT("  FROM pg_operator op\n")
        wxT("  LEFT JOIN pg_type tl ON tl.oid=op.oprleft\n")
        wxT("  LEFT JOIN pg_type tr ON tr.oid=op.oprright\n")
        wxT(" WHERE oprowner = ") + uid + wxT(" AND op.oid > ") + sysoid + wxT("\n")
        wxT(" ORDER BY 1,2,3"));
            
    form->EndMsg();
}


void pgUser::ShowTreeDetail(wxTreeCtrl *browser, frmMain *form, ctlListView *properties, ctlSQLBox *sqlPane)
{
    if (!expandedKids)
    {
        expandedKids=true;

        pgSet *set=GetServer()->ExecuteSet(wxT("SELECT groname, grolist FROM pg_group ORDER BY groname"));
        if (set)
        {
            while (!set->Eof())
            {
                wxString groupName=set->GetVal(wxT("groname"));
                wxString str=set->GetVal(wxT("grolist"));
                if (!str.IsNull())
                {
                    wxStringTokenizer ids(str.Mid(1, str.Length()-2), wxT(","));
                    while (ids.HasMoreTokens())
                    {
                        if (StrToLong(ids.GetNextToken()) == GetUserId())
                        {
                            groupsIn.Add(groupName);
                            break;
                        }
                    }
                }
                set->MoveNext();
            }
            delete set;
        }
    }
    if (properties)
    {
        wxLogInfo(wxT("Displaying properties for User ") + GetIdentifier());

        CreateListColumns(properties);

        properties->AppendItem(_("Name"), GetName());
        properties->AppendItem(_("User ID"), GetUserId());
        properties->AppendItem(_("Account expires"), GetAccountExpires());
        properties->AppendItem(_("Superuser?"), BoolToYesNo(GetSuperuser()));
        properties->AppendItem(_("Create databases?"), BoolToYesNo(GetCreateDatabase()));
        properties->AppendItem(_("Update catalogs?"), BoolToYesNo(GetUpdateCatalog()));

        wxString groupList;

        size_t index;
        for (index=0 ; index < groupsIn.GetCount() ; index++)
        {
            if (!groupList.IsEmpty())
                groupList += wxT(", ");
            groupList += groupsIn.Item(index);
        }
        properties->AppendItem(wxT("Member of"), groupList);

        for (index=0; index < configList.GetCount() ; index++)
        {
            wxString item=configList.Item(index);
            properties->AppendItem(item.BeforeFirst('='), item.AfterFirst('='));
        }
    }
}



pgObject *pgUser::Refresh(wxTreeCtrl *browser, const wxTreeItemId item)
{
    pgObject *user=0;
    wxTreeItemId parentItem=browser->GetItemParent(item);
    if (parentItem)
    {
        pgObject *obj=(pgObject*)browser->GetItemData(parentItem);
        if (obj->GetType() == PG_USERS)
            user = ReadObjects((pgCollection*)obj, 0, wxT("\n WHERE usesysid=") + NumToStr(GetUserId()));
    }
    return user;
}



pgObject *pgUser::ReadObjects(pgCollection *collection, wxTreeCtrl *browser, const wxString &restriction)
{
    pgUser *user=0;

    wxString tabname;

    if (collection->GetServer()->HasPrivilege(wxT("table"), wxT("pg_shadow"), wxT("SELECT")))
        tabname=wxT("pg_shadow");
    else
        tabname=wxT("pg_user");

    pgSet *users = collection->GetServer()->ExecuteSet(wxT(
        "SELECT * FROM ") + tabname + restriction + wxT(" ORDER BY usename"));

    if (users)
    {
        while (!users->Eof())
        {

            user = new pgUser(users->GetVal(wxT("usename")));
            user->iSetServer(collection->GetServer());
            user->iSetUserId(users->GetLong(wxT("usesysid")));
            user->iSetCreateDatabase(users->GetBool(wxT("usecreatedb")));
            user->iSetSuperuser(users->GetBool(wxT("usesuper")));
            user->iSetUpdateCatalog(users->GetBool(wxT("usecatupd")));
            user->iSetAccountExpires(users->GetDateTime(wxT("valuntil")));
            user->iSetPassword(users->GetVal(wxT("passwd")));

            wxString cfg=users->GetVal(wxT("useconfig"));
            if (!cfg.IsEmpty())
                FillArray(user->GetConfigList(), cfg.Mid(1, cfg.Length()-2));

            if (browser)
            {
                collection->AppendBrowserItem(browser, user);
				users->MoveNext();
            }
            else
                break;
        }

		delete users;
    }
    return user;
}
