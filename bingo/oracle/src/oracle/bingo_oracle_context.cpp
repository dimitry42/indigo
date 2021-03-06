/****************************************************************************
 * Copyright (C) 2009-2012 GGA Software Services LLC
 * 
 * This file is part of Indigo toolkit.
 * 
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 3 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ***************************************************************************/

#include "oracle/bingo_oracle.h"
#include "base_cpp/output.h"
#include "molecule/molecule.h"
#include "molecule/molecule_tautomer.h"
#include "oracle/bingo_oracle_context.h"
#include "core/bingo_error.h"
#include "core/bingo_context.h"
#include "oracle/ora_wrap.h"
#include "base_cpp/scanner.h"
#include "oracle/ora_logger.h"
#include "base_cpp/shmem.h"
#include "base_cpp/auto_ptr.h"
#include "molecule/elements.h"

BingoOracleContext::BingoOracleContext (OracleEnv &env, int id_) :
BingoContext(id_),
storage(env, id_),
_config_changed(false)
{
   ArrayOutput output(_id);

   output.printf("BINGO_%d", id_);
   output.writeChar(0);

   _shmem = 0;
}

BingoOracleContext::~BingoOracleContext ()
{
   delete _shmem;
}

BingoOracleContext & BingoOracleContext::get (OracleEnv &env, int id, bool lock, bool *config_reloaded)
{
   BingoOracleContext *already = (BingoOracleContext *)_get(id);

   if (config_reloaded != 0)
      *config_reloaded = false;

   if (already != 0)
   {
      if (lock)
         already->lock(env);
      if (already->_config_changed)
      {
         env.dbgPrintfTS("reloading config\n");
         already->_loadConfigParameters(env);
         if (config_reloaded != 0)
            *config_reloaded = true;
      }
      return *already;
   }

   AutoPtr<BingoOracleContext> res(new BingoOracleContext(env, id));

   if (lock)
      res->lock(env);
   res->_loadConfigParameters(env);
   if (config_reloaded != 0)
      *config_reloaded = true;
   
   OsLocker locker(_instances_lock);
   TL_GET(PtrArray<BingoContext>, _instances);

   _instances.add(res.release());

   return *(BingoOracleContext *)_instances.top();
}

void BingoOracleContext::_loadConfigParameters (OracleEnv &env)
{
   fingerprintLoadParameters(env);
   tautomerLoadRules(env);
   atomicMassLoad(env);

   int txap = 0;
   int icbdm = 0;

   configGetInt(env, "TREAT_X_AS_PSEUDOATOM", txap);
   treat_x_as_pseudoatom = (txap != 0);

   configGetInt(env, "IGNORE_CLOSING_BOND_DIRECTION_MISMATCH", icbdm);
   ignore_closing_bond_direction_mismatch = (icbdm != 0);

   QS_DEF(Array<char>, cmfdict);
   
   if (configGetBlob(env, "CMFDICT", cmfdict))
   {
      BufferScanner scanner(cmfdict);

      cmf_dict.load(scanner);
   }

   QS_DEF(Array<char>, riddict);

   if (configGetBlob(env, "RIDDICT", riddict))
   {
      BufferScanner scanner(riddict);

      rid_dict.load(scanner);
   }
   
   configGetInt(env, "SUB_SCREENING_PASS_MARK", sub_screening_pass_mark);
   configGetInt(env, "SIM_SCREENING_PASS_MARK", sim_screening_pass_mark);
   configGetInt(env, "SUB_SCREENING_MAX_BITS", sub_screening_max_bits);

   _config_changed = false;
}

void BingoOracleContext::saveCmfDict (OracleEnv &env)
{
   env.dbgPrintfTS("saving cmf dictionary\n");

   QS_DEF(Array<char>, cmfdict);
   
   ArrayOutput output(cmfdict);
   cmf_dict.saveFull(output);
   cmf_dict.resetModified();
   
   configSetBlob(env, "CMFDICT", cmfdict);
}

void BingoOracleContext::saveRidDict (OracleEnv &env)
{
   env.dbgPrintfTS("saving rowid dictionary\n");

   QS_DEF(Array<char>, riddict);

   ArrayOutput output(riddict);
   rid_dict.saveFull(output);
   rid_dict.resetModified();

   configSetBlob(env, "RIDDICT", riddict);
}

bool BingoOracleContext::configGetInt (OracleEnv &env, const char *name, int &value)
{
   if (!OracleStatement::executeSingleInt(value, env, "SELECT value FROM "
      "(SELECT value FROM config_int WHERE name = upper('%s') AND n in (0, %d) "
      "ORDER BY n DESC) WHERE rownum <= 1", name, id))
      return false;

   return true;
}

void BingoOracleContext::configResetAll (OracleEnv &env)
{
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_INT WHERE n = %d", id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_STR WHERE n = %d", id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_FLOAT WHERE n = %d", id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_BLOB WHERE n = %d", id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_CLOB WHERE n = %d", id);
   _config_changed = true;
}

void BingoOracleContext::configReset (OracleEnv &env, const char *name)
{
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_INT WHERE name = upper('%s') AND n = %d",
      name, id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_STR WHERE name = upper('%s') AND n = %d",
      name, id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_FLOAT WHERE name = upper('%s') AND n = %d",
      name, id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_BLOB WHERE name = upper('%s') AND n = %d",
      name, id);
   OracleStatement::executeSingle(env, "DELETE FROM CONFIG_CLOB WHERE name = upper('%s') AND n = %d",
      name, id);
   _config_changed = true;
}

void BingoOracleContext::configSetInt (OracleEnv &env, const char *name, int value)
{
   configReset(env, name);
   OracleStatement::executeSingle(env, "INSERT INTO CONFIG_INT VALUES(%d, upper('%s'), %d)",
      id, name, value);
   _config_changed = true;
}

bool BingoOracleContext::configGetFloat (OracleEnv &env, const char *name, float &value)
{
   if (!OracleStatement::executeSingleFloat(value, env, "SELECT value FROM "
      "(SELECT value FROM CONFIG_FLOAT WHERE name = upper('%s') AND n in (0, %d) "
      "ORDER BY n DESC) WHERE rownum <= 1", name, id))
      return false;

   return true;
}

void BingoOracleContext::configSetFloat (OracleEnv &env, const char *name, float value)
{
   configReset(env, name);
   OracleStatement::executeSingle(env, "INSERT INTO CONFIG_FLOAT VALUES(%d, upper('%s'), %f)",
      id, name, value);
   _config_changed = true;
}

bool BingoOracleContext::configGetString (OracleEnv &env, const char *name, Array<char> &value)
{
   if (!OracleStatement::executeSingleString(value, env, "SELECT value FROM "
         "(SELECT value FROM config_str WHERE name = upper('%s') AND n in (0, %d) "
         "ORDER BY n DESC) WHERE rownum <= 1", name, id))
      return false;

   return true;
}

void BingoOracleContext::configSetString (OracleEnv &env, const char *name, const char *value)
{
   configReset(env, name);
   OracleStatement::executeSingle(env, "INSERT INTO CONFIG_STR VALUES(%d, upper('%s'), '%s')",
      id, name, value);
   _config_changed = true;
}

bool BingoOracleContext::configGetBlob (OracleEnv &env, const char *name, Array<char> &value)
{
   if (!OracleStatement::executeSingleBlob(value, env, "SELECT value FROM "
      "(SELECT value FROM CONFIG_BLOB WHERE name = upper('%s') AND n in (0, %d) "
      "ORDER BY n DESC) WHERE rownum <= 1", name, id))
      return false;

   return true;
}

void BingoOracleContext::configSetBlob (OracleEnv &env, const char *name, const Array<char> &value)
{
   configReset(env, name);

   OracleLOB lob(env);
   OracleStatement statement(env);

   lob.createTemporaryBLOB();
   lob.write(0, value.ptr(), value.size());
   statement.append("INSERT INTO config_blob VALUES (%d, upper('%s'), :value)",
      id, name);

   statement.prepare();
   statement.bindBlobByName(":value", lob);
   statement.execute();

   _config_changed = true;
}

bool BingoOracleContext::configGetClob (OracleEnv &env, const char *name, Array<char> &value)
{
   if (!OracleStatement::executeSingleClob(value, env, "SELECT value FROM "
      "(SELECT value FROM CONFIG_CLOB WHERE name = upper('%s') AND n in (0, %d) "
      "ORDER BY n DESC) WHERE rownum <= 1", name, id))
      return false;

   return true;
}

void BingoOracleContext::configSetClob (OracleEnv &env, const char *name, const Array<char> &value)
{
   configReset(env, name);

   OracleLOB lob(env);
   OracleStatement statement(env);

   lob.createTemporaryCLOB();
   lob.write(0, value.ptr(), value.size());
   statement.append("INSERT INTO config_clob VALUES (%d, upper('%s'), :value)",
      id, name);

   statement.prepare();
   statement.bindBlobByName(":value", lob);
   statement.execute();

   _config_changed = true;
}

void BingoOracleContext::tautomerLoadRules (OracleEnv &env)
{
   tautomer_rules.clear();

   OracleStatement statement(env);
   int n;
   char param1[128], param2[128];

   statement.append("SELECT id, beg, end FROM tautomer_rules ORDER BY id ASC");
   statement.prepare();
   statement.defineIntByPos(1, &n);
   statement.defineStringByPos(2, param1, sizeof(param1));
   statement.defineStringByPos(3, param2, sizeof(param2));

   if (statement.executeAllowNoData()) do
   {
      if (n < 1 || n >= 32)
         throw BingoError("tautomer rule index %d is out of range", n);

      AutoPtr<TautomerRule> rule(new TautomerRule());

      bingoGetTauCondition(param1, rule->aromaticity1, rule->list1);
      bingoGetTauCondition(param2, rule->aromaticity2, rule->list2);

      tautomer_rules.expand(n);
      tautomer_rules[n - 1] = rule.release();
   } while (statement.fetch());
}

void BingoOracleContext::fingerprintLoadParameters (OracleEnv &env)
{
   configGetInt(env, "FP_ORD_SIZE", fp_parameters.ord_qwords);
   configGetInt(env, "FP_TAU_SIZE", fp_parameters.tau_qwords);
   configGetInt(env, "FP_SIM_SIZE", fp_parameters.sim_qwords);
   configGetInt(env, "FP_ANY_SIZE", fp_parameters.any_qwords);
   fp_parameters.ext = true;
   fp_parameters_ready = true;
   
   configGetInt(env, "FP_STORAGE_CHUNK", fp_chunk_qwords);
}

void BingoOracleContext::longOpInit (OracleEnv &env, int total, const char *operation,
                                     const char *target, const char *units)
{
   _longop_slno = 0;
   _longop_total = total;
   _longop_operation.readString(operation, true);
   _longop_target.readString(target, true);
   _longop_units.readString(units, true);

   OracleStatement statement(env);

   statement.append("BEGIN :longop_rindex := DBMS_APPLICATION_INFO.set_session_longops_nohint; END;");
   statement.prepare();
   statement.bindIntByName(":longop_rindex", &_longop_rindex);
   statement.execute();
}

void BingoOracleContext::longOpUpdate (OracleEnv &env, int sofar)
{
   OracleStatement statement(env);

   statement.append("BEGIN dbms_application_info.set_session_longops("
      "rindex    => :longop_rindex, "
      "slno      => :longop_slno, "
      "op_name   => '%s', sofar => %d, totalwork => %d, target_desc => '%s', "
      "units => '%s'); END;",
      _longop_operation.ptr(), sofar, _longop_total, _longop_target.ptr(), _longop_units.ptr());

   statement.prepare();
   statement.bindIntByName(":longop_rindex", &_longop_rindex);
   statement.bindIntByName(":longop_slno",   &_longop_slno);
   statement.execute();
}

void BingoOracleContext::parseParameters (OracleEnv &env, const char *str)
{
   BufferScanner scanner(str);

   QS_DEF(Array<char>, param_name);

   static const char *PARAMETERS_INT[] = 
   {
      "FP_ORD_SIZE",
      "FP_TAU_SIZE",
      "FP_ORD_BPC",
      "FP_TAU_BPC",
      "FP_MAX_CYCLE_LEN",
      "FP_MIN_TREE_EDGES",
      "FP_MAX_TREE_EDGES",
      "FP_STORAGE_CHUNK"
   };

   bool config_changed = false;

   scanner.skipSpace();
   while (!scanner.isEOF())
   {
      scanner.readWord(param_name, " =");
      scanner.skipSpace();
      if (scanner.readChar() != '=')
         throw Error("can't parse parameters: '%s'", str);

      scanner.skipSpace();

      bool parameter_found = false;
      for (int i = 0; i < NELEM(PARAMETERS_INT); i++)
         if (strcasecmp(param_name.ptr(), PARAMETERS_INT[i]) == 0)
         {
            int value = scanner.readInt();
            configSetInt(env, PARAMETERS_INT[i], value);

            parameter_found = true;
            config_changed = true;
            break;
         }

      if (strcasecmp(param_name.ptr(), "NTHREADS") == 0)
      {
         nthreads = scanner.readInt();
         parameter_found = true;
      }

      if (!parameter_found)
         throw Error("unknown parameter %s", param_name.ptr());

      scanner.skipSpace();
   }

   if (config_changed)
      _loadConfigParameters(env);
}


void BingoOracleContext::atomicMassLoad (OracleEnv &env)
{
   relative_atomic_mass_map.clear();

   if (!configGetString(env, "RELATIVE_ATOMIC_MASS", _relative_atomic_mass))
      return;

   const char *buffer = _relative_atomic_mass.ptr();
   QS_DEF(Array<char>, element_str);
   element_str.resize(_relative_atomic_mass.size());

   float mass;
   int pos;

   while (sscanf(buffer, "%s%f%n", element_str.ptr(), &mass, &pos) > 1)
   {
      int elem = Element::fromString(element_str.ptr());
      if (relative_atomic_mass_map.find(elem))
         throw Error("element '%s' duplication in atomic mass list", element_str.ptr());

      relative_atomic_mass_map.insert(elem, mass);
      buffer += pos;

      while (*buffer == ' ')
         buffer++;
      if (buffer[0] == ';')
         buffer++;
   }

   // Print debug information
   if (relative_atomic_mass_map.size() != 0)
   {
      env.dbgPrintfTS("Relative atomic mass read: ");
      for (int i = relative_atomic_mass_map.begin();
               i != relative_atomic_mass_map.end();
               i = relative_atomic_mass_map.next(i))
      {
         int elem = relative_atomic_mass_map.key(i);
         float mass = relative_atomic_mass_map.value(i);
         env.dbgPrintf("%s %g; ", Element::toString(elem), mass);
      }
      env.dbgPrintf("\n");
   }
}

void BingoOracleContext::atomicMassSave (OracleEnv &env)
{
   if (_relative_atomic_mass.size() > 1)
      configSetString(env, "RELATIVE_ATOMIC_MASS", _relative_atomic_mass.ptr());
   else
      configSetString(env, "RELATIVE_ATOMIC_MASS", "");
}


void BingoOracleContext::lock (OracleEnv &env)
{
   // TODO: implement a semaphore?
   env.dbgPrintf("    locking %s... ", _id.ptr());

   if (_shmem != 0)
   {
      env.dbgPrintf("already locked\n");
      return;
   }

   while (1)
   {
      _shmem = new SharedMemory(_id.ptr(), 1, false);

      if (_shmem->wasFirst())
         break;

      delete _shmem;
   }
   env.dbgPrintf("locked\n");
}

void BingoOracleContext::unlock (OracleEnv &env)
{
   if (_shmem == 0)
   {
      env.dbgPrintf("%s is not locked by this process\n", _id.ptr());
      return;
   }
   env.dbgPrintf("unlocking %s\n", _id.ptr());
   delete _shmem;
   _shmem = 0;
}
