/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2018 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "fvPatchFieldMapper.H"
#include "volFields.H"
#include "basicThermo.H"
#include "addToRunTimeSelectionTable.H"
#include "fixedHumidityFvPatchScalarField.H"

class heHumidityRhoThermo;


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fixedHumidityFvPatchScalarField::
fixedHumidityFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF
)
:
    fixedValueFvPatchScalarField(p, iF),
    mode_("relative"),
    method_("buck"),
    value_(0.0),
    methodName_
    (
        IOobject
        (
            "methodName",
            p.boundaryMesh().mesh().time().timeName(),
            p.boundaryMesh().mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        1
    )
{}


Foam::fixedHumidityFvPatchScalarField::
fixedHumidityFvPatchScalarField
(
    const fixedHumidityFvPatchScalarField& ptf,
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const fvPatchFieldMapper& mapper
)
:
    fixedValueFvPatchScalarField(ptf, p, iF, mapper),
    mode_(ptf.mode_),
    method_(ptf.method_),
    value_(ptf.value_),
    methodName_(ptf.methodName_)
{}


Foam::fixedHumidityFvPatchScalarField::
fixedHumidityFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const dictionary& dict
)
:
    fixedValueFvPatchScalarField(p, iF, dict),
    mode_(dict.lookupOrDefault<word>("mode", "relative")),
    method_(dict.lookupOrDefault<word>("method", "buck")),
    value_(readScalar(dict.lookup("humidity"))),
    methodName_
    (
        IOobject
        (
            "methodName",
            p.boundaryMesh().mesh().time().timeName(),
            p.boundaryMesh().mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        1
    )
{
    // Default method to calculate the saturation pressure
    methodName_[0] = "buck";

    if (mode_ == "absolute")
    {
       Info<< "The specific value of the humidity is set to " << value_
           << " g/m^3";
    }
    else if (mode_ == "specific")
    {
       Info<< "The specific value of the humidity is set to " << value_
           << " g/kg";
    }

    else if (mode_ == "relative")
    {
        if (method_ != "buck" && method_ != "magnus")
        {
            FatalErrorInFunction
                << "The specified method to calculate the saturation pressure is "
                << "not supported: " << method_ << ". Supported methods are "
                << "'buck' and 'magnus'."
                << exit(FatalError);
        }

        // Set the method for the thermodynamic lib
        methodName_[0] = method_;
    }
    else
    {
        FatalErrorInFunction
            << "The specified type is not supported '"
            << mode_ << "'. Supported are 'relative' or 'absolute'"
            << exit(FatalError);
    }
}


Foam::fixedHumidityFvPatchScalarField::
fixedHumidityFvPatchScalarField
(
    const fixedHumidityFvPatchScalarField& tppsf
)
:
    fixedValueFvPatchScalarField(tppsf),
    mode_(tppsf.mode_),
    method_(tppsf.method_),
    value_(tppsf.value_),
    methodName_(tppsf.methodName_)
{}


Foam::fixedHumidityFvPatchScalarField::
fixedHumidityFvPatchScalarField
(
    const fixedHumidityFvPatchScalarField& tppsf,
    const DimensionedField<scalar, volMesh>& iF
)
:
    fixedValueFvPatchScalarField(tppsf, iF),
    mode_(tppsf.mode_),
    method_(tppsf.method_),
    value_(tppsf.value_),
    methodName_(tppsf.methodName_)
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void Foam::fixedHumidityFvPatchScalarField::updateCoeffs()
{
    if (updated())
    {
        return;
    }

    const basicThermo& thermo = basicThermo::lookupThermo(*this);
    const label patchi = patch().index();

    const scalarField specificHumidity = calcSpecificHumidity(thermo, patchi);

    //const scalarField& pw = thermo.p().boundaryField()[patchi];
    //fvPatchScalarField& Tw =
    //    const_cast<fvPatchScalarField&>(thermo.T().boundaryField()[patchi]);
    //Tw.evaluate();
    operator==(specificHumidity);

    fixedValueFvPatchScalarField::updateCoeffs();
}


const Foam::scalarField Foam::fixedHumidityFvPatchScalarField::calcSpecificHumidity
(
    const basicThermo& thermo,
    const label patchi
)
{
    //- a) Calc saturation pressure
    const scalarField& pfT = thermo.T().boundaryField()[patchi];
    const scalarField& pfp = thermo.p().boundaryField()[patchi];

    if (mode_ == "relative")
    {
        const scalarField theta = pfT - 273.15; //(Tpatch.size(), scalar(0));

        scalarField pSatH2O(pfT.size(), scalar(0));

        //  Standard method not as accurate as buck
        if (method_ == "magnus")
        {
            const scalar pre1 = 611.2;
            const scalar pre2 = 17.62;
            const scalar value1 = 243.12;

            forAll(pSatH2O, facei)
            {
                pSatH2O[facei] =
                    pre1*exp((pre2*(theta[facei]))/(value1+theta[facei]));
            }
        }
        //  Buck formula [1996]
        //  Valid between 0 to 100 degC and 1013.25 hPa
        //  Very accurate between 0 degC and 50 degC
        else if (method_ == "buck")
        {
            const scalar pre1 = 611.21;
            const scalar value1 = 18.678;
            const scalar value2 = 234.5;
            const scalar value3 = 257.14;

            forAll(pSatH2O, facei)
            {
                scalar TdC = theta[facei];

                pSatH2O[facei] =
                    pre1*exp(((value1-TdC/value2)*TdC/(value3+TdC)));
            }
        }

        //- b) Calc partial pressure of water
        scalarField partialPressureH2O(pfT.size(), scalar(0));
        partialPressureH2O = value_ * pSatH2O;

        //- c) Calc density of water [kg/m^3]
        scalarField rhoWater(pfT.size(), scalar(0));
        {
            const scalar RH2O = 461.51;
            rhoWater = partialPressureH2O/(RH2O*pfT);
        }

        //- d) Calc density of dry air [kg/m^3]
        scalarField rhoDryAir(pfT.size(), scalar(0));
        {
            const scalar RdryAir = 287.058;
            rhoDryAir = (pfp - partialPressureH2O)/(RdryAir*pfT);
        }

        //- e) Calculate the specific humidity [kg/kg]
        //scalarField specificHumidity(pfT.size(), scalar(0));

        return rhoWater/(rhoWater+rhoDryAir);
    }
    else if (mode_ == "specific")
    {
        //- Convert [g/kg] to [kg/kg]
        scalarField specificHumidity(pfT.size(), value_/1000);

        return specificHumidity;
    }
    else if (mode_ == "absolute")
    {
        //- b) Calc partial pressure of the water
        const scalar RH2O = 461.51;
        scalarField partialPressureH2O(pfT.size(), scalar(0));

        //- The absolute humidity is in [g/m^3]. For the formulation we need
        //  [kg/m^3] --> / 1000
        partialPressureH2O = value_/1000 * pfT * RH2O;

        //- c) Calc density of water [kg/m^3]
        scalarField rhoWater(pfT.size(), scalar(0));
        {
            const scalar RH2O = 461.51;
            rhoWater = partialPressureH2O/(RH2O*pfT);
        }

        //- d) Calc density of dry air [kg/m^3]
        scalarField rhoDryAir(pfT.size(), scalar(0));
        {
            const scalar RdryAir = 287.058;
            rhoDryAir = (pfp - partialPressureH2O)/(RdryAir*pfT);
        }

        //- e) Calculate the specific humidity [kg/kg]
        //scalarField specificHumidity(pfT.size(), scalar(0));

        return rhoWater/(rhoWater+rhoDryAir);
    }
    else
    {
        Info<< "The mode " << mode_ << " is not available in the fixedHumidity"
            << " boundary condition." << endl;

        FatalErrorInFunction
            << "The specified type is not supported '"
            << mode_ << "'. Supported are 'relative' or 'specific'"
            << exit(FatalError);

        //- For compiling purposes
        {
            scalarField specificHumidity(pfT.size(), 0);

            return specificHumidity;
        }
    }
}


void Foam::fixedHumidityFvPatchScalarField::write(Ostream& os) const
{
    fvPatchScalarField::write(os);
    os.writeKeyword("mode") << mode_ << token::END_STATEMENT << nl;
    os.writeKeyword("method") << method_ << token::END_STATEMENT << nl;
    os.writeKeyword("humidity") << value_ << token::END_STATEMENT << nl;
    writeEntry("value", os);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
    makePatchTypeField
    (
        fvPatchScalarField,
        fixedHumidityFvPatchScalarField
    );
}

// ************************************************************************* //
