/*
 * This source file is part of the Alexandria program.
 *
 * Copyright (C) 2014-2018 
 *
 * Developers:
 *             Mohammad Mehdi Ghahremanpour, 
 *             Paul J. van Maaren, 
 *             David van der Spoel (Project leader)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */
 
/*! \internal \brief
 * Implements part of the alexandria program.
 * \author Mohammad Mehdi Ghahremanpour <mohammad.ghahremanpour@icm.uu.se>
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 */

#include "molprop_tables.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gromacs/fileio/xvgr.h"
#include "gromacs/linearalgebra/matrix.h"
#include "gromacs/math/utilities.h"
#include "gromacs/math/vec.h"
#include "gromacs/statistics/statistics.h"
#include "gromacs/utility/cstringutil.h"

#include "categories.h"
#include "composition.h"
#include "latex_util.h"

namespace alexandria
{

typedef struct {
    char       *ptype;
    char       *miller;
    char       *bosque;
    gmx_stats_t lsq;
    int         nexp;
    int         nqm;
} t_sm_lsq;


static void stats_header(LongTable         &lt,
                         MolPropObservable  mpo,
                         const QmCount     &qmc,
                         iMolSelect         ims)
{
    char caption[STRLEN];
    char label[STRLEN];

    lt.setColumns(1+qmc.nCalc());

    for (int k = 0; (k < 2); k++)
    {
        if (0 == k)
        {
            snprintf(caption, STRLEN, "Performance of the different methods for predicting the molecular %s for molecules containing different chemical groups, given as the RMSD from experimental values (%s), and in brackets the number of molecules in this particular subset. {\\bf Data set: %s.} At the bottom the correlation coefficient R, the regression coefficient a and the intercept b are given as well as the normalized quality of the fit $\\chi^2$, the mean signed error (MSE) and the mean absolute error (MSA).",
                     mpo_name[mpo], mpo_unit[mpo], iMolSelectName(ims));
            snprintf(label, STRLEN, "%s_rmsd", mpo_name[mpo]);
            lt.setCaption(caption);
            lt.setLabel(label);
        }
        else
        {
            char hline[STRLEN];
            char koko[STRLEN];
            snprintf(hline, STRLEN, "Method ");
            for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
            {
                snprintf(koko, STRLEN, " & %s", q->method().c_str());
                strncat(hline, koko, STRLEN-strlen(hline)-1);
            }
            lt.addHeadLine(hline);

            snprintf(hline, STRLEN, " ");
            for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
            {
                snprintf(koko, STRLEN, "& %s ", q->basis().c_str());
                strncat(hline, koko, STRLEN-strlen(hline)-1);
            }
            lt.addHeadLine(hline);

            snprintf(hline, STRLEN, " ");
            for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
            {
                snprintf(koko, STRLEN, "& %s ", q->type().c_str());
                strncat(hline, koko, STRLEN-strlen(hline)-1);
            }
            lt.addHeadLine(hline);
        }
    }
    lt.printHeader();
}

void alexandria_molprop_stats_table(FILE                 *fp,
                                    MolPropObservable     mpo,
                                    std::vector<MolProp> &mp,
                                    const QmCount        &qmc,
                                    char                 *exp_type,
                                    double                outlier,
                                    CategoryList          cList,
                                    const MolSelect      &gms,
                                    iMolSelect            ims)
{
    std::vector<MolProp>::iterator     mpi;
    std::vector<std::string>::iterator si;
    int                                N;
    double                             exp_val, qm_val;
    real                               rms, R, a, da, b, db, chi2;
    char                               buf[256];
    gmx_stats_t                        lsq;
    std::vector<gmx_stats_t>           lsqtot;
    LongTable                          lt(fp, true, nullptr);
    CompositionSpecs                   cs;
    const char                        *alex = cs.searchCS(iCalexandria)->name();

    if (0 == cList.nCategories())
    {
        fprintf(stderr, "No categories. cList not initialized? Not doing category statistics.\n");
        return;
    }

    stats_header(lt, mpo, qmc, ims);

    for (auto i = cList.beginCategories(); (i < cList.endCategories()); ++i)
    {
        std::string catbuf;
        int         nqmres  = 0;
        int         nexpres = 0;
        snprintf(buf, sizeof(buf), "%s", i->getName().c_str());
        catbuf.append(buf);
        for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
        {
            lsq = gmx_stats_init();
            for (auto &mpi : mp)
            {
                if ((i->hasMolecule(mpi.getIupac())) &&
                    (mpi.SearchCategory(i->getName()) == 1))
                {
                    double exp_err = 0;
                    double Texp    = -1;
                    bool   bQM     = false;
                    bool   bExp    = mpi.getProp(mpo, iqmExp, "", "",
                                                 exp_type, &exp_val, &exp_err, &Texp);
                    if (bExp)
                    {
                        double qm_err = 0;
                        double Tqm    = -1;
                        bQM    = mpi.getProp(mpo, iqmQM, q->lot(), "",
                                             q->type(), &qm_val, &qm_err, &Tqm);
                        //printf("Texp %g Tqm %g bQM = %s\n", Texp, Tqm, bQM ? "true" : "false");
                        if (bQM)
                        {
                            if (debug)
                            {
                                fprintf(debug, "%s %s - TAB4\n",
                                        mpi.getMolname().c_str(),
                                        i->getName().c_str());
                            }
                            gmx_stats_add_point(lsq, exp_val, qm_val, exp_err, qm_err);
                            nexpres = 1;
                        }
                    }
                    if (debug)
                    {
                        fprintf(debug, "STATSTAB: bQM %s bExp %s mol %s\n",
                                gmx::boolToString(bQM), gmx::boolToString(bExp),
                                mpi.getMolname().c_str());
                    }
                }
            }
            if (outlier > 0)
            {
                gmx_stats_remove_outliers(lsq, outlier);
            }
            if ((gmx_stats_get_rmsd(lsq, &rms) == estatsOK) &&
                (gmx_stats_get_npoints(lsq, &N) == estatsOK))
            {
                sprintf(buf, "& %8.1f(%d)", rms, N);
                catbuf.append(buf);
                nqmres++;
            }
            else
            {
                catbuf.append("& -");
            }

            gmx_stats_free(lsq);
        }
        if ((nqmres > 0) && (nexpres > 0))
        {
            lt.printLine(catbuf);
        }
    }
    std::string catbuf;
    catbuf.assign("All");
    lsqtot.resize(qmc.nCalc());
    int k = 0;
    for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q, ++k)
    {
        lsqtot[k] = gmx_stats_init();
        for (mpi = mp.begin(); (mpi < mp.end()); mpi++)
        {
            if ((gms.status(mpi->getIupac()) == ims) &&
                (mpi->HasComposition(alex)))
            {
                double exp_err, qm_err;
                double Texp = -1;
                bool   bExp = mpi->getProp(mpo, iqmExp, "", "", exp_type,
                                           &exp_val, &exp_err, &Texp);
                double Tqm  = Texp;
                bool   bQM  = mpi->getProp(mpo, iqmQM, q->lot(), "", q->type(),
                                           &qm_val, &qm_err, &Tqm);
                if (bExp && bQM)
                {
                    gmx_stats_add_point(lsqtot[k], exp_val, qm_val, exp_err, qm_err);
                }
            }
        }
        if ((gmx_stats_get_rmsd(lsqtot[k], &rms) == estatsOK) &&
            (gmx_stats_get_npoints(lsqtot[k], &N) == estatsOK))

        {
            snprintf(buf, sizeof(buf),  "& %8.1f(%d)", rms, N);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);
    lt.printHLine();

    catbuf.assign("a");
    for (auto &k : lsqtot)
    {
        if (gmx_stats_get_ab(k, elsqWEIGHT_NONE, &a, &b, &da, &db, &chi2, &R) ==
            estatsOK)
        {
            snprintf(buf, sizeof(buf), "& %8.2f(%4.2f)", a, da);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);

    catbuf.assign("b");
    for (auto &k : lsqtot)
    {
        if (gmx_stats_get_ab(k, elsqWEIGHT_NONE, &a, &b, &da, &db, &chi2, &R) ==
            estatsOK)
        {
            snprintf(buf, sizeof(buf), "& %8.2f(%4.2f)", b, db);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);

    catbuf.assign("R$^2$ (\\%)");
    for (auto &k : lsqtot)
    {
        if (gmx_stats_get_corr_coeff(k, &R) == estatsOK)
        {
            snprintf(buf, sizeof(buf), "& %8.2f", 100*R*R);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);

    catbuf.assign("$\\chi^2$");
    for (auto &k : lsqtot)
    {
        if (gmx_stats_get_ab(k, elsqWEIGHT_NONE, &a, &b, &da, &db, &chi2, &R) ==
            estatsOK)
        {
            snprintf(buf, sizeof(buf), "& %8.2f", chi2);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);

    catbuf.assign("MSE");
    for (auto &k : lsqtot)
    {
        real mse;
        if (gmx_stats_get_mse_mae(k, &mse, nullptr) ==
            estatsOK)
        {
            snprintf(buf, sizeof(buf), "& %8.2f", mse);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);

    catbuf.assign("MAE");
    for (auto &k : lsqtot)
    {
        real mae;
        if (gmx_stats_get_mse_mae(k, nullptr, &mae) ==
            estatsOK)
        {
            snprintf(buf, sizeof(buf), "& %8.2f", mae);
            catbuf.append(buf);
        }
        else
        {
            catbuf.append("& -");
        }
    }
    lt.printLine(catbuf);
    lt.printFooter();

    for (auto &k : lsqtot)
    {
        gmx_stats_free(k);
    }
}

static void composition_header(LongTable             &lt,
                               iMolSelect             ims)
{
    char caption[STRLEN];

    snprintf(caption, STRLEN, "Decomposition of molecules into Alexandria atom types. {\\bf Data set: %s.} Charge is given when not zero, multiplicity is given when not 1.",
             iMolSelectName(ims));
    lt.setCaption(caption);
    lt.setLabel("frag_defs");
    lt.setColumns("p{75mm}ll");
    lt.addHeadLine("Molecule & Formula  & Types");
    lt.printHeader();
}

void alexandria_molprop_composition_table(FILE                 *fp, 
                                          std::vector<MolProp>  mp,
                                          const MolSelect      &gms, 
                                          iMolSelect            ims)
{
    std::vector<MolProp>::iterator             mpi;
    MolecularCompositionIterator               mci;
    int                                        q, m, iline, nprint;
    char                                       qbuf[32], longbuf[STRLEN], buf[256];
    LongTable                                  lt(fp, true, "small");
    CompositionSpecs                           cs;
    const char                                *alex = cs.searchCS(iCalexandria)->name();

    nprint = 0;
    for (mpi = mp.begin(); (mpi < mp.end()); mpi++)
    {
        if ((ims == gms.status(mpi->getIupac())) &&
            (mpi->HasComposition(alex)))
        {
            nprint++;
        }
    }
    if (nprint <= 0)
    {
        return;
    }

    composition_header(lt, ims);
    iline = 0;
    for (mpi = mp.begin(); (mpi < mp.end()); mpi++)
    {
        if ((ims == gms.status(mpi->getIupac())) &&
            (mpi->HasComposition(alex)))
        {
            q = mpi->getCharge();
            m = mpi->getMultiplicity();
            if ((q != 0) || (m != 1))
            {
                if ((q != 0) && (m != 1))
                {
                    sprintf(qbuf, " (q=%c%d, mult=%d)", (q < 0) ? '-' : '+', abs(q), m);
                }
                else if (q != 0)
                {
                    sprintf(qbuf, " (q=%c%d)", (q < 0) ? '-' : '+', abs(q));
                }
                else
                {
                    sprintf(qbuf, " (mult=%d)", m);
                }
            }
            else
            {
                qbuf[0] = '\0';
            }
            snprintf(longbuf, STRLEN, "%3d. %s%s & %s & ",
                     ++iline,
                     mpi->getIupac().c_str(),
                     qbuf,
                     mpi->getTexFormula().c_str());
            mci = mpi->SearchMolecularComposition(cs.searchCS(iCalexandria)->name());
            if (mci != mpi->EndMolecularComposition())
            {
                AtomNumIterator ani;

                for (ani = mci->BeginAtomNum(); (ani < mci->EndAtomNum()); ani++)
                {
                    snprintf(buf, 256, " %d %s\t",
                             ani->getNumber(), ani->getAtom().c_str());
                    strncat(longbuf, buf, STRLEN-sizeof(longbuf)-1);
                }
            }
            lt.printLine(longbuf);
        }
    }
    lt.printFooter();
}


static void category_header(LongTable &lt)
{
    char longbuf[STRLEN];

    lt.setColumns("lcp{150mm}");
    snprintf(longbuf, sizeof(longbuf),
             "Molecules that are part of each category used for statistics.");
    lt.setCaption(longbuf);
    lt.setLabel("stats");
    lt.addHeadLine("Category & N & Molecule(s)");
    lt.printHeader();
}

void alexandria_molprop_category_table(FILE            *fp,
                                       int              catmin,
                                       CategoryList     cList)
{
    if (cList.nCategories() > 0)
    {
        LongTable lt(fp, true, "small");
        category_header(lt);
        for (CategoryListElementIterator i = cList.beginCategories();
             (i < cList.endCategories()); ++i)
        {
            std::string longbuf;
            char        buf[256];
            int         nMol = i->nMolecule();

            if ((nMol >= catmin) && (catmin > 1))
            {
                int n = 0;
                std::vector<std::string>::iterator j;
                snprintf(buf, sizeof(buf), "%s & %d &", i->getName().c_str(), nMol);
                longbuf.append(buf);
                for (j = i->beginMolecules(); (j < i->endMolecules()-1); ++j)
                {
                    snprintf(buf, sizeof(buf), "%s, ", j->c_str());
                    longbuf.append(buf);
                    n++;
                    if (0 == (n % 50))
                    {
                        lt.printLine(longbuf);
                        longbuf.assign(" & &");
                    }
                }
                snprintf(buf, sizeof(buf), "%s", j->c_str());
                longbuf.append(buf);
                lt.printLine(longbuf);
            }
        }
        lt.printFooter();
    }
}

static void atomtype_tab_header(LongTable &lt)
{
    char             longbuf[STRLEN];
    CompositionSpecs cs;

    lt.setColumns("ccccccc");

    snprintf(longbuf, STRLEN, "Atomic polarizability obtained from the decomposition of the experimental isotropic molecular polarizability. $N$ is the number of experimental datapoints used. The columns Ahc and Ahp contain atomic hybrid components~\\protect\\cite{Miller1979a} and atomic hybrid polarizabilites~\\protect\\cite{Miller1990a, Kang1982a}, respectively. The column BS contains the polarizabilities of Bosque and Sales~\\protect\\cite{Bosque2002a}. The atom types are according to the General Amber Force Field~\\cite{Wang2004a}. The uncertainty, $\\sigma$, in the Alexandria polarizability values are computed by Bootstrapping with 1000 interations.");
    lt.setCaption(longbuf);
    lt.setLabel("fragments");
    snprintf(longbuf, STRLEN, "Atom Type  & $N$ & \\multicolumn{4}{c}{Polarizability}");
    lt.addHeadLine(longbuf);
    snprintf(longbuf, STRLEN, "& & %s ($\\sigma$) & Ahc & Ahp & %s ",
             cs.searchCS(iCalexandria)->name(),
             cs.searchCS(iCbosque)->abbreviation());
    lt.addHeadLine(longbuf);
    lt.printHeader();
}

static void alexandria_molprop_atomtype_polar_table(FILE                 *fp,
                                                    const Poldata        &pd,
                                                    std::vector<MolProp>  mp,
                                                    const char           *lot,
                                                    const char           *exp_type)
{
    std::vector<MolProp>::iterator  mpi;
    double                          ahc, ahp, bos_pol;
    char                            longbuf[STRLEN];
    MolPropObservable               mpo = MPO_POLARIZABILITY;
    LongTable                       lt(fp, false, nullptr);
    CompositionSpecs                cs;
    const char                     *alexandria = cs.searchCS(iCalexandria)->name();

    /* Prepare printing it! */
    atomtype_tab_header(lt);

    /* First gather statistics from different input files.
     * Note that the input files
     * do not need to have the same sets of types,
     * as we check for the type name.
     */
    for (auto pType = pd.getPtypeBegin(); pType != pd.getPtypeEnd(); pType++)
    {
        if (pType->getPolarizability() > 0)
        {
            int atomnumber;
            int nexp   = 0;
            int nqm    = 0;

            /* Now loop over the poltypes and count number of occurrences
             * in molecules
             */
            for (auto &mpi : mp)
            {
                auto mci = mpi.SearchMolecularComposition(alexandria);

                if (mci != mpi.EndMolecularComposition())
                {
                    bool bFound = false;
                    for (auto ani = mci->BeginAtomNum(); !bFound && (ani < mci->EndAtomNum()); ++ani)
                    {
                        std::string pt;
                        if (pd.atypeToPtype(ani->getAtom(), pt))
                        {
                            if(pt == pType->getType())
                            {
                                bFound = true;
                            }
                        }
                    }
                    if (bFound)
                    {
                        double val, T = -1;
                        if (mpi.getProp(mpo, iqmExp, lot, "", exp_type, &val, nullptr, &T))
                        {
                            nexp++;
                        }
                        else if (mpi.getProp(mpo, iqmQM, lot, "", (char *)"electronic", &val, nullptr, &T))
                        {
                            nqm++;
                        }
                    }
                }
            }

            /* Determine Miller and Bosque polarizabilities for this Alexandria element */
            ahc = ahp = bos_pol = 0;
            std::string aequiv;
            if (1 == pd.getMillerPol(pType->getMiller(), &atomnumber, &ahc, &ahp, aequiv))
            {
                ahc = (4.0/atomnumber)*gmx::square(ahc);
            }
            if (0 == pd.getBosquePol(pType->getBosque(), &bos_pol))
            {
                bos_pol = 0;
            }
            
            size_t      pos   = pType->getType().find("p_");
            std::string ptype = pType->getType();
            if (pos != std::string::npos)
            {
                ptype = ptype.substr(pos+2);
            }
            snprintf(longbuf, STRLEN, "%s & %s & %s (%s) & %s & %s & %s",
                     ptype.c_str(),
                     (nexp > 0)     ? gmx_itoa(nexp).c_str()     : "-",
                     (pType->getPolarizability() > 0)  ? gmx_ftoa(pType->getPolarizability()).c_str()  : "-",
                     (pType->getSigPol() > 0)  ? gmx_ftoa(pType->getSigPol()).c_str() : "-",
                     (ahc > 0)         ? gmx_ftoa(ahc).c_str()         : "-",
                     (ahp > 0)         ? gmx_ftoa(ahp).c_str()         : "-",
                     (bos_pol > 0)     ? gmx_ftoa(bos_pol).c_str()     : "-");
            lt.printLine(longbuf);
        }
    }
    lt.printFooter();
    fflush(fp);
}

static void alexandria_molprop_atomtype_dip_table(FILE          *fp,
                                                  const Poldata &pd)
{
    int         i, k, m, cur = 0;
    std::string gt_type[2] = { "", "" };

#define prev (1-cur)
#define NEQG 6
    ChargeDistributionModel eqgcol[NEQG] = { eqdAXp, eqdAXpp, eqdAXg, eqdAXpg,  eqdAXs, eqdAXps};
    char                    longbuf[STRLEN], buf[256];
    int                     npcol[NEQG]  = { 2, 3, 3 };
    const char             *clab[3]      = { "$J_0$", "$\\chi_0$", "$\\zeta$" };
    int                     ncol;
    LongTable               lt(fp, true, nullptr);

    ncol = 1;
    for (i = 0; (i < NEQG); i++)
    {
        ncol += npcol[i];
    }
    lt.setCaption("Electronegativity equalization parameters for Alexandria models. $J_0$ and $\\chi_0$ in eV, $\\zeta$ in 1/nm.");
    lt.setLabel("eemparams");
    lt.setColumns(ncol);

    snprintf(longbuf, STRLEN, " ");
    for (i = 0; (i < NEQG); i++)
    {
        snprintf(buf, 256, " & \\multicolumn{%d}{c}{%s}", npcol[i],
                 getEemtypeName(eqgcol[i]));
        strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
    }
    lt.addHeadLine(longbuf);

    snprintf(longbuf, STRLEN, " ");
    for (i = 0; (i < NEQG); i++)
    {
        for (m = 0; (m < npcol[i]); m++)
        {
            snprintf(buf, 256, " & %s", clab[m]);
            strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
        }
    }
    lt.addHeadLine(longbuf);
    lt.printHeader();

    for (auto aType = pd.getAtypeBegin(); aType != pd.getAtypeEnd(); aType++)
    {
        gt_type[cur] = aType->getType();
        if (((0 == gt_type[prev].size()) || (strcmp(gt_type[cur].c_str(), gt_type[cur].c_str()) != 0)))
        {
            snprintf(longbuf, STRLEN, "%s\n", gt_type[cur].c_str());
            for (k = 0; (k < NEQG); k++)
            {
                if (pd.haveEemSupport(eqgcol[k], gt_type[cur], false))
                {
                    snprintf(buf, 256, " & %.3f", pd.getJ00(eqgcol[k], gt_type[cur]));
                    strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
                    snprintf(buf, 256, " & %.3f", pd.getChi0(eqgcol[k], gt_type[cur]));
                    strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
                    if (npcol[k] == 3)
                    {
                        snprintf(buf, 256, " & %.3f", pd.getZeta(eqgcol[k], gt_type[cur], 1));
                        strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
                    }
                    if (npcol[k] == 4)
                    {
                        snprintf(buf, 256, " & %.3f", pd.getZeta(eqgcol[k], gt_type[cur], 2));
                        strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
                    }
                }
                else
                {
                    for (m = 0; (m < npcol[k]); m++)
                    {
                        snprintf(buf, 256, " & ");
                        strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
                    }
                }
            }
            lt.printLine(longbuf);
        }
        cur = prev;
    }
    lt.printFooter();
}

void alexandria_molprop_atomtype_table(FILE                       *fp,
                                       bool                        bPolar,
                                       const Poldata              &pd,
                                       const std::vector<MolProp> &mp,
                                       const char                 *lot,
                                       const char                 *exp_type)
{
    if (bPolar)
    {
        alexandria_molprop_atomtype_polar_table(fp, pd, mp, lot, exp_type);
    }
    else
    {
        alexandria_molprop_atomtype_dip_table(fp, pd);
    }
}

static void prop_header(LongTable     &lt,
                        const char    *property,
                        const char    *unit,
                        real           rel_toler,
                        real           abs_toler,
                        const QmCount  qmc,
                        iMolSelect     ims,
                        bool           bPrintConf,
                        bool           bPrintBasis,
                        bool           bPrintMultQ)
{
    int  i, k, nc;
    char columns[256];
    char longbuf[STRLEN];
    char buf[256];

    snprintf(columns, 256, "p{75mm}");
    nc = 2 + qmc.nCalc();
    if (bPrintMultQ)
    {
        nc += 2;
    }
    if (bPrintConf)
    {
        nc++;
    }
    for (i = 0; (i < nc); i++)
    {
        strncat(columns, "c", 256-strlen(columns)-1);
    }
    lt.setColumns(columns);

    for (k = 0; (k < 2); k++)
    {
        if (0 == k)
        {
            snprintf(longbuf, STRLEN, "Comparison of experimental %s to calculated values. {\\bf Data set: %s}. Calculated numbers that are more than %.0f%s off the experimental values are printed in bold, more than %.0f%s off in bold red.",
                     property, iMolSelectName(ims),
                     (abs_toler > 0) ? abs_toler   : 100*rel_toler,
                     (abs_toler > 0) ? unit : "\\%",
                     (abs_toler > 0) ? 2*abs_toler : 200*rel_toler,
                     (abs_toler > 0) ? unit : "\\%");
            lt.setCaption(longbuf);
            snprintf(longbuf, STRLEN, "%s", iMolSelectName(ims));
            lt.setLabel(longbuf);
        }
        else
        {
            snprintf(longbuf, STRLEN, "Molecule & Form. %s %s & Exper. ",
                     bPrintMultQ ? "& q & mult" : "",
                     bPrintConf  ? "& Conf." : "");
            for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
            {
                snprintf(buf, 256, "& %s", q->method().c_str());
                strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
            }
            lt.addHeadLine(longbuf);

            if (bPrintBasis)
            {
                snprintf(longbuf, STRLEN, " & & %s %s",
                         bPrintMultQ ? "& &" : "",
                         bPrintConf ? "&" : "");
                for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
                {
                    snprintf(buf, 256, "& %s", q->basis().c_str());
                    strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
                }
                lt.addHeadLine(longbuf);
            }
            snprintf(longbuf, STRLEN, "Type & &%s %s",
                     bPrintMultQ ? "& &" : "",
                     bPrintConf ? "&" : "");
            for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
            {
                snprintf(buf, 256, "& %s", q->type().c_str());
                strncat(longbuf, buf, STRLEN-strlen(longbuf)-1);
            }
            lt.addHeadLine(longbuf);
        }
    }
    lt.printHeader();
}

static int outside(real vexp, real vcalc, real rel_toler, real abs_toler)
{
    real rdv, adv = fabs(vexp-vcalc);
    if (abs_toler > 0)
    {
        if (adv > 2*abs_toler)
        {
            return 2;
        }
        else if (adv > abs_toler)
        {
            return 1;
        }
        return 0;
    }
    else
    {
        if (vexp == 0)
        {
            return 0;
        }
        rdv = adv/vexp;

        if (rdv > 2*rel_toler)
        {
            return 2;
        }
        else if (rdv > rel_toler)
        {
            return 1;
        }
        return 0;
    }
}

void alexandria_molprop_prop_table(FILE                 *fp,
                                   MolPropObservable     mpo,
                                   real                  rel_toler,
                                   real                  abs_toler,
                                   std::vector<MolProp> &mp,
                                   const QmCount        &qmc,
                                   const char           *exp_type,
                                   bool                  bPrintAll,
                                   bool                  bPrintBasis,
                                   bool                  bPrintMultQ,
                                   const MolSelect      &gms,
                                   iMolSelect            ims)
{
    MolecularQuadrupoleIterator qi;
    MolecularEnergyIterator     mei;

    int                         iprint = 0;
#define BLEN 1024
    char                        myline[BLEN], mylbuf[BLEN], vbuf[BLEN];
    double                      calc_val, calc_err, vc;
    double                      dvec[DIM];
    tensor                      quadrupole;
    bool                        bPrintConf;
    CompositionSpecs            cs;
    const char                 *alex = cs.searchCS(iCalexandria)->name();

    LongTable                   lt(fp, true, "small");

    int nprint = std::count_if(mp.begin(), mp.end(),
                               [ims, alex, gms](const MolProp &mpi)
                               { return ((ims == gms.status(mpi.getIupac())) &&
                                         (mpi.HasComposition(alex))); });
    if (nprint <= 0)
    {
        return;
    }
    bPrintConf = false; //(mpo == MPO_DIPOLE);
    prop_header(lt, mpo_name[mpo], mpo_unit[mpo],
                rel_toler, abs_toler, qmc,
                ims, bPrintConf, bPrintBasis, bPrintMultQ);
    for (auto &mpi : mp)
    {
        if ((ims == gms.status(mpi.getIupac())) &&
            (mpi.HasComposition(alex)))
        {
            std::vector<ExpData>  ed;
            std::vector<CalcData> cd;
            for (auto ei = mpi.BeginExperiment(); (ei < mpi.EndExperiment()); ei++)
            {
                switch (mpo)
                {
                    case MPO_DIPOLE:
                        for (auto mdi = ei->BeginDipole(); (mdi < ei->EndDipole()); mdi++)
                        {
                            if (mdi->getType().compare(exp_type) == 0)
                            {
                                ed.push_back(ExpData(mdi->getAver(),
                                                     mdi->getError(),
                                                     mdi->getTemperature(),
                                                     ei->getReference(),
                                                     ei->getConformation(),
                                                     mdi->getType(),
                                                     mdi->getUnit()));
                            }
                        }
                        break;
                    case MPO_POLARIZABILITY:
                        for (auto mdi = ei->BeginPolar(); (mdi < ei->EndPolar()); mdi++)
                        {
                            if (mdi->getType().compare(exp_type) == 0)
                            {
                                ed.push_back(ExpData(mdi->getAverage(),
                                                     mdi->getError(),
                                                     mdi->getTemperature(),
                                                     ei->getReference(),
                                                     ei->getConformation(),
                                                     mdi->getType(),
                                                     mdi->getUnit()));
                            }
                        }
                        break;
                    case MPO_ENERGY:
                    case MPO_ENTROPY:
                        for (mei = ei->BeginEnergy(); (mei < ei->EndEnergy()); mei++)
                        {
                            if (mei->getType().compare(exp_type) == 0)
                            {
                                ed.push_back(ExpData(mei->getValue(),
                                                     mei->getError(),
                                                     mei->getTemperature(),
                                                     ei->getReference(),
                                                     ei->getConformation(),
                                                     mei->getType(),
                                                     mei->getUnit()));
                            }
                        }
                        break;
                    default:
                        gmx_fatal(FARGS, "No support for for mpo %d", mpo);
                        break;
                }
            }
            int nqm = 0;
            for (int nexp = 0; (nexp < (int)ed.size()); nexp++)
            {
                for (auto q = qmc.beginCalc(); q < qmc.endCalc(); ++q)
                {
                    std::string ref, mylot;
                    double      T = ed[nexp].temp_;
                    if ((q->type().compare(exp_type) == 0) &&
                        mpi.getPropRef(mpo, iqmQM, q->lot(), "",
                                       q->type(), &calc_val, &calc_err, &T,
                                       ref, mylot, dvec, quadrupole))
                    {
                        cd.push_back(CalcData(calc_val, calc_err, T, 1));
                        nqm++;
                    }
                    else
                    {
                        cd.push_back(CalcData(0, 0, 0, 0));
                    }
                }
                if (nullptr != debug)
                {
                    fprintf(debug, "Found %d experiments and %d calculations for %s\n",
                            (int)ed.size(), nqm, exp_type);
                }
                if ((bPrintAll || (ed.size() > 0))  && (nqm > 0))
                {
                    if (0 == nexp)
                    {
                        iprint++;
                    }
                    myline[0] = '\0';
                    if (nexp == 0)
                    {
                        if (bPrintMultQ)
                        {
                            snprintf(myline, BLEN, "%d. %-15s & %s & %d & %d",
                                     iprint, mpi.getIupac().c_str(),
                                     mpi.getTexFormula().c_str(),
                                     mpi.getCharge(),
                                     mpi.getMultiplicity());
                        }
                        else
                        {
                            snprintf(myline, BLEN, "%d. %-15s & %s",
                                     iprint, mpi.getIupac().c_str(),
                                     mpi.getTexFormula().c_str());
                        }
                    }
                    else
                    {
                        snprintf(myline, BLEN, " & ");
                    }
                    if (bPrintConf)
                    {
                        snprintf(mylbuf, BLEN, "      & %s ", ((ed[nexp].conf_.size() > 0) ?
                                                               ed[nexp].conf_.c_str() : "-"));
                        strncat(myline, mylbuf, BLEN-strlen(myline)-1);
                    }
                    if (ed.size() > 0)
                    {
                        sprintf(mylbuf, "& %8.3f", ed[nexp].val_);
                        strncat(myline, mylbuf, BLEN-strlen(myline)-1);
                        if (ed[nexp].err_ > 0)
                        {
                            sprintf(mylbuf, "(%.3f)", ed[nexp].err_);
                            strncat(myline, mylbuf, BLEN-strlen(myline)-1);
                        }
                        if (strcmp(ed[nexp].ref_.c_str(), "Maaren2017a") == 0)
                        {
                            sprintf(mylbuf, " (*)");
                        }
                        else
                        {
                            sprintf(mylbuf, "~\\cite{%s} ", ed[nexp].ref_.c_str());
                        }
                        strncat(myline, mylbuf, BLEN-strlen(myline)-1);
                    }
                    else
                    {
                        sprintf(mylbuf, "& - ");
                        strncat(myline, mylbuf, BLEN-strlen(myline)-1);
                    }
                    for (size_t j = 0; (j < qmc.nCalc()); j++)
                    {
                        if (cd[j].found_ > 0)
                        {
                            vc = cd[j].val_;
                            if (cd[j].err_ > 0)
                            {
                                sprintf(vbuf, "%8.2f(%.2f)", vc, cd[j].err_);
                            }
                            else
                            {
                                sprintf(vbuf, "%8.2f", vc);
                            }
                            if (ed.size() > 0)
                            {
                                int oo = outside(ed[nexp].val_, vc, rel_toler, abs_toler);
                                switch (oo)
                                {
                                    case 2:
                                        sprintf(mylbuf, "& \\textcolor{Red}{\\bf %s} ", vbuf);
                                        break;
                                    case 1:
                                        sprintf(mylbuf, "& {\\bf %s} ", vbuf);
                                        break;
                                    default:
                                        sprintf(mylbuf, "& %s ", vbuf);
                                }
                            }
                            else
                            {
                                sprintf(mylbuf, "& %s ", vbuf);
                            }
                            strncat(myline, mylbuf, BLEN-strlen(myline)-1);
                        }
                        else
                        {
                            sprintf(mylbuf, "& ");
                            strncat(myline, mylbuf, BLEN-strlen(myline));
                        }
                    }
                    lt.printLine(myline);
                }
            }
        }
    }
    lt.printFooter();
}

} // namespace