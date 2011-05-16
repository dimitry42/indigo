/****************************************************************************
 * Copyright (C) 2010-2011 GGA Software Services LLC
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

#include "indigo_internal.h"
#include "layout/reaction_layout.h"
#include "layout/molecule_layout.h"
#include "reaction/base_reaction.h"
#include "indigo_molecule.h"
#include "indigo_reaction.h"

CEXPORT int indigoLayout (int object)
{
   INDIGO_BEGIN
   {
      IndigoObject &obj = self.getObject(object);
      int i;

      if (IndigoBaseMolecule::is(obj)) {
         BaseMolecule &mol = obj.getBaseMolecule();
         MoleculeLayout ml(mol);
         ml.max_iterations = self.layout_max_iterations;
         ml.bond_length = 1.6f;
         ml.make();
         mol.stereocenters.markBonds();
         for (i = 1; i <= mol.rgroups.getRGroupCount(); i++)
         {
            RGroup &rgp = mol.rgroups.getRGroup(i);

            for (int j = rgp.fragments.begin(); j != rgp.fragments.end();
                     j = rgp.fragments.next(j))
            {
               MoleculeLayout fragml(*rgp.fragments[j]);

               fragml.max_iterations = self.layout_max_iterations;
               fragml.bond_length = 1.6f;
               fragml.make();
               rgp.fragments[j]->stereocenters.markBonds();
            }
         }
      } else if (IndigoBaseReaction::is(obj)) {
         BaseReaction &rxn = obj.getBaseReaction();
         ReactionLayout rl(rxn);
         rl.max_iterations = self.layout_max_iterations;
         rl.bond_length = 1.6f;
         rl.make();
         rxn.markStereocenterBonds();
      } else {
         throw IndigoError("The object provided is neither a molecule, nor a reaction");
      }
      return 0;
   }
   INDIGO_END(-1)
}