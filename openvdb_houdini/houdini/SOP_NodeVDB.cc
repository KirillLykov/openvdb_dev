///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
//
/// @file SOP_NodeVDB.cc
/// @author FX R&D OpenVDB team

#include "SOP_NodeVDB.h"

#include "Utils.h"
#include "GEO_PrimVDB.h"
#include "GU_PrimVDB.h"
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <OP/OP_NodeInfoParms.h>
#include <PRM/PRM_Parm.h>
#include <PRM/PRM_Type.h>
#include <sstream>


namespace openvdb_houdini {

SOP_NodeVDB::SOP_NodeVDB(OP_Network* net, const char* name, OP_Operator* op):
    SOP_Node(net, name, op)
{
#ifndef SESI_OPENVDB
    // Initialize the vdb library
    openvdb::initialize();
#endif

    // Set the flag to draw guide geometry
    mySopFlags.setNeedGuide1(true);

    // We can use this to optionally draw the local data window?
    // mySopFlags.setNeedGuide2(true);
}


////////////////////////////////////////


const GA_PrimitiveGroup*
SOP_NodeVDB::matchGroup(GU_Detail& aGdp, const std::string& pattern)
{
    /// @internal Presumably, when a group name pattern matches multiple groups,
    /// a new group must be created that is the union of the matching groups,
    /// and therefore the detail must be non-const.  Since inputGeo() returns
    /// a const detail, we can't match groups in input details; however,
    /// we usually copy input 0 to the output detail, so we can in effect
    /// match groups from input 0 by matching them in the output instead.

    const GA_PrimitiveGroup* group = NULL;
    if (!pattern.empty()) {
        // If a pattern was provided, try to match it.
        group = parsePrimitiveGroups(pattern.c_str(), &aGdp);
        if (!group) {
            // Report an error if the pattern didn't match.
            throw std::runtime_error(("Invalid group (" + pattern + ")").c_str());
        }
    }
    return group;
}


////////////////////////////////////////


void
SOP_NodeVDB::getNodeSpecificInfoText(OP_Context &context, OP_NodeInfoParms &parms)
{
    SOP_Node::getNodeSpecificInfoText(context, parms);

#ifdef SESI_OPENVDB
    // Nothing needed since we will report it as part of native prim info
#else
    // Get a handle to the geometry.
    GU_DetailHandle gd_handle = getCookedGeoHandle(context);

   // Check if we have a valid detail handle.
    if(gd_handle.isNull()) return;

    // Lock it for reading.
    GU_DetailHandleAutoReadLock gd_lock(gd_handle);
    // Finally, get at the actual GU_Detail.
    const GU_Detail* tmp_gdp = gd_lock.getGdp();

    std::ostringstream infoStr;

    unsigned gridn = 0;
    for (VdbPrimCIterator it(tmp_gdp); it; ++it) {

        const openvdb::GridBase& grid = it->getGrid();
        openvdb::Coord dim = grid.evalActiveVoxelDim();
        const UT_String gridName = it.getPrimitiveName();

        infoStr << "    ";
        infoStr << "(" << it.getIndex() << ")";
        if(gridName.isstring()) infoStr << " name: '" << gridName << "',";
        infoStr << " voxel size: " << grid.transform().voxelSize()[0] << ",";
        infoStr << " type: "<< grid.valueType() << ",";

        if (grid.activeVoxelCount() != 0) {
            infoStr << " dim: " << dim[0] << "x" << dim[1] << "x" << dim[2];
        } else {
            infoStr <<" <empty>";
        }

        infoStr<<"\n";

        ++gridn;
    }

    if (gridn > 0) {
        std::ostringstream headStr;
        headStr << gridn << " VDB grid" << (gridn == 1 ? "" : "s") << "\n";

        parms.append(headStr.str().c_str());
        parms.append(infoStr.str().c_str());
    }
#endif
}


////////////////////////////////////////


namespace {

void
createEmptyGridGlyph(GU_Detail& gdp, GridCRef grid)
{
    openvdb::Vec3R lines[6];

    lines[0].init(-0.5, 0.0, 0.0);
    lines[1].init( 0.5, 0.0, 0.0);
    lines[2].init( 0.0,-0.5, 0.0);
    lines[3].init( 0.0, 0.5, 0.0);
    lines[4].init( 0.0, 0.0,-0.5);
    lines[5].init( 0.0, 0.0, 0.5);

    const openvdb::math::Transform &xform = grid.transform();
    lines[0] = xform.indexToWorld(lines[0]);
    lines[1] = xform.indexToWorld(lines[1]);
    lines[2] = xform.indexToWorld(lines[2]);
    lines[3] = xform.indexToWorld(lines[3]);
    lines[4] = xform.indexToWorld(lines[4]);
    lines[5] = xform.indexToWorld(lines[5]);

    boost::shared_ptr<GU_Detail> tmpGDP(new GU_Detail);

    UT_Vector3 color(0.1, 1.0, 0.1);
    tmpGDP->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3, GA_Defaults(color.data(), 3));

    GU_PrimPoly *poly;

    for (int i = 0; i < 6; i += 2) {
        poly = GU_PrimPoly::build(&*tmpGDP, 2, GU_POLY_OPEN);

        tmpGDP->setPos3(poly->getPointOffset(i % 2),
            UT_Vector3(lines[i][0], lines[i][1], lines[i][2]));

        tmpGDP->setPos3(poly->getPointOffset(i % 2 + 1),
            UT_Vector3(lines[i + 1][0], lines[i + 1][1], lines[i + 1][2]));
    }

    gdp.merge(*tmpGDP);
}

} // unnamed namespace


OP_ERROR
SOP_NodeVDB::cookMyGuide1(OP_Context& context)
{
    myGuide1->clearAndDestroy();
    UT_Vector3 color(0.1, 0.1, 1.0);
    UT_Vector3 corners[8];

    // For each VDB primitive (with a non-null grid pointer) in the group...
    for (VdbPrimIterator it(gdp); it; ++it) {
        if (evalGridBBox(it->getGrid(), corners, /*expandHalfVoxel=*/true)) {
            houdini_utils::createBox(*myGuide1, corners, &color);
        } else {
            createEmptyGridGlyph(*myGuide1, it->getGrid());
        }
    }

    return error();
}


////////////////////////////////////////


openvdb::Vec3f
SOP_NodeVDB::evalVec3f(const char *name, fpreal time) const
{
    return openvdb::Vec3f(evalFloat(name, 0, time),
                          evalFloat(name, 1, time),
                          evalFloat(name, 2, time));
}

openvdb::Vec3R
SOP_NodeVDB::evalVec3R(const char *name, fpreal time) const
{
    return openvdb::Vec3R(evalFloat(name, 0, time),
                          evalFloat(name, 1, time),
                          evalFloat(name, 2, time));
}

openvdb::Vec3i
SOP_NodeVDB::evalVec3i(const char *name, fpreal time) const
{
    return openvdb::Vec3i(evalInt(name, 0, time),
                          evalInt(name, 1, time),
                          evalInt(name, 2, time));
}

openvdb::Vec2R
SOP_NodeVDB::evalVec2R(const char *name, fpreal time) const
{
    return openvdb::Vec2R(evalFloat(name, 0, time),
                          evalFloat(name, 1, time));
}

openvdb::Vec2i
SOP_NodeVDB::evalVec2i(const char *name, fpreal time) const
{
    return openvdb::Vec2i(evalInt(name, 0, time),
                          evalInt(name, 1, time));
}


////////////////////////////////////////


void
SOP_NodeVDB::resolveRenamedParm(PRM_ParmList& obsoleteParms,
    const char* oldName, const char* newName)
{
    PRM_Parm* parm = obsoleteParms.getParmPtr(oldName);
    if (parm && !parm->isFactoryDefault()) {
        if (this->hasParm(newName)) {
            this->getParm(newName).copyParm(*parm);
        }
    }
}


////////////////////////////////////////


namespace {

/// @brief OpPolicy for OpenVDB operator types at SESI
class SESIOpenVDBOpPolicy: public houdini_utils::OpPolicy
{
public:
    virtual std::string getName(const houdini_utils::OpFactory&, const std::string& english)
    {
        UT_String s(english);
        // Lowercase
        s.toLower();
        // Remove non-alphanumeric characters from the name.
        s.forceValidVariableName();
        std::string name = s.toStdString();
        // Remove spaces and underscores.
        name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
        name.erase(std::remove(name.begin(), name.end(), '_'), name.end());
        return name;
    }

    /// @brief OpenVDB operators of each flavor (SOP, POP, etc.) share
    /// an icon named "SOP_OpenVDB", "POP_OpenVDB", etc.
    virtual std::string getIconName(const houdini_utils::OpFactory& factory)
    {
        return factory.flavorString() + "_OpenVDB";
    }
};


/// @brief OpPolicy for OpenVDB operator types at DWA
class DWAOpenVDBOpPolicy: public houdini_utils::DWAOpPolicy
{
public:
    /// @brief OpenVDB operators of each flavor (SOP, POP, etc.) share
    /// an icon named "SOP_OpenVDB", "POP_OpenVDB", etc.
    virtual std::string getIconName(const houdini_utils::OpFactory& factory)
    {
        return factory.flavorString() + "_OpenVDB";
    }
};


#ifdef SESI_OPENVDB
typedef SESIOpenVDBOpPolicy OpenVDBOpPolicy;
#else
typedef DWAOpenVDBOpPolicy  OpenVDBOpPolicy;
#endif // SESI_OPENVDB

} // unnamed namespace


OpenVDBOpFactory::OpenVDBOpFactory(
    const std::string& english,
    OP_Constructor ctor,
    houdini_utils::ParmList& parms,
    OP_OperatorTable& table,
    houdini_utils::OpFactory::OpFlavor flavor):
    houdini_utils::OpFactory(OpenVDBOpPolicy(), english, ctor, parms, table, flavor)
{
}

} // namespace openvdb_houdini

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
