#include "global.h"
#include "constants/rgb.h"

#include "rogue_colour_utils.h"
#include "rogue_settings.h"

u16 const gDefaultPaletteLayerMasks[PALETTE_MODIFY_LAYER_COUNT] =
{
    RGB(31, 0, 0),
    RGB(0, 31, 0),
    RGB(0, 0, 31),
};

u16 HSVToRGB(struct HSV hsv)
{
    u8 r, g, b;
    u8 region, remainder, p, q, t;
    
    if (hsv.s == 0)
    {
        r = hsv.v;
        g = hsv.v;
        b = hsv.v;
        return RGB_RANGE_255_TO_31(r, g, b);
    }
    
    region = hsv.h / 43;
    remainder = (hsv.h - (region * 43)) * 6; 
    
    p = (hsv.v * (255 - hsv.s)) >> 8;
    q = (hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8;
    t = (hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region)
    {
        case 0:
            r = hsv.v; g = t; b = p;
            break;
        case 1:
            r = q; g = hsv.v; b = p;
            break;
        case 2:
            r = p; g = hsv.v; b = t;
            break;
        case 3:
            r = p; g = q; b = hsv.v;
            break;
        case 4:
            r = t; g = p; b = hsv.v;
            break;
        default:
            r = hsv.v; g = p; b = q;
            break;
    }
    
    return RGB_RANGE_255_TO_31(r, g, b);
}

struct HSV RGBtoHSV(u16 rgb)
{
    struct HSV hsv;
    u8 r, g, b, rgbMin, rgbMax;

    r = RANGE_31_TO_255(GET_R(rgb));
    g = RANGE_31_TO_255(GET_G(rgb));
    b = RANGE_31_TO_255(GET_B(rgb));

    rgbMin = r < g ? (r < b ? r : b) : (g < b ? g : b);
    rgbMax = r > g ? (r > b ? r : b) : (g > b ? g : b);
    
    hsv.v = rgbMax;
    if (hsv.v == 0)
    {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }

    hsv.s = 255 * (s64)(rgbMax - rgbMin) / hsv.v;
    if (hsv.s == 0)
    {
        hsv.h = 0;
        return hsv;
    }

    if (rgbMax == r)
        hsv.h = 0 + 43 * (g - b) / (rgbMax - rgbMin);
    else if (rgbMax == g)
        hsv.h = 85 + 43 * (b - r) / (rgbMax - rgbMin);
    else
        hsv.h = 171 + 43 * (r - g) / (rgbMax - rgbMin);

    return hsv;
}

// Preserve neutral colours and a broad range of skin-like tones.
#define TINT_MIN_VALUE   80    // por debajo de este brillo se considera contorno/negro
#define SKIN_YELLOW_HUE  35    // límite superior (piel clara/amarillenta)
#define SKIN_PINK_HUE    220   // desde aquí hasta 255 (y de 0 a 26) es matiz de piel
#define SKIN_SAT_MAX     120   // más saturado que esto es ropa o cinta, no piel

#define MOUTH_HUE_MAX    10    // solo rojos/anaranjados (la boca tiene matiz 2-7)
#define MOUTH_SAT_MAX    215   // la sombra de la boca llega a saturación 210

#define SKIN_ORANGE_HUE    12    // hasta aquí (y desde SKIN_PINK_HUE) vale SKIN_SAT_MAX
#define SKIN_PALE_SAT_MAX  60    // por encima de este matiz, solo es piel si es muy pálida
#define MOUTH_SAT_MIN      150   // la boca real tiene saturación 170-210; el salmón, 120-135

static bool8 IsSkinTone(struct HSV hsv)
{
    bool8 pinkOrOrange = hsv.h <= SKIN_ORANGE_HUE || hsv.h >= SKIN_PINK_HUE;
    bool8 yellowish = hsv.h > SKIN_ORANGE_HUE && hsv.h <= SKIN_YELLOW_HUE;

    bool8 skin = hsv.s >= 8 && hsv.v >= 120
        && ((pinkOrOrange && hsv.s <= SKIN_SAT_MAX) || (yellowish && hsv.s <= SKIN_PALE_SAT_MAX));
    bool8 darkSkin = (pinkOrOrange || yellowish) && hsv.s >= 8
        && hsv.s <= SKIN_DARK_SAT_MAX && hsv.v > TINT_MIN_VALUE;
    bool8 mouth = hsv.h <= MOUTH_HUE_MAX && hsv.s >= MOUTH_SAT_MIN
        && hsv.s <= MOUTH_SAT_MAX && hsv.v >= 150;

    return skin || darkSkin || mouth;
}

static bool8 IsColourExemptFromHueTint(u8 slot, struct HSV hsv)
{
    return slot == 0 || slot == 2 || hsv.s == 0 || hsv.v <= TINT_MIN_VALUE || IsSkinTone(hsv);
}

static u8 GetHueDistance(u8 a, u8 b)
{
    u8 d = (u8)(a - b);
    return d < 128 ? d : 256 - d;
}

static u8 GetAverageTintHue(u16 const* inputPal)
{
    u8 i, ref = 0;
    s32 sum = 0;
    u32 count = 0;

    for(i = 0; i < 16; ++i)
    {
        struct HSV hsv = RGBtoHSV(inputPal[i]);

        if(!IsColourExemptFromHueTint(i, hsv))
        {
            if(count == 0)
                ref = hsv.h;

            sum += (s8)(u8)(hsv.h - ref);
            ++count;
        }
    }

    return count == 0 ? 0 : (u8)(ref + sum / (s32)count);
}

static u8 GetFurthestTintHue(u16 const* inputPal, u8 targetHue)
{
    u8 i;
    u8 hue = 0;
    u8 maxDist = 0;

    for(i = 0; i < 16; ++i)
    {
        u16 rgb = inputPal[i];
        struct HSV hsv = RGBtoHSV(rgb);

        if(!IsColourExemptFromHueTint(i, hsv))
        {
            u8 currDist = GetHueDistance(targetHue, hsv.h);
            if(currDist > maxDist)
            {
                hue = hsv.h;
                maxDist = currDist;
            }
        }
    }

    return hue;
}

static u8 GetNearestTintHue(u16 const* inputPal, u8 targetHue)
{
    u8 i;
    u8 hue = 0;
    u8 maxDist = 0;

    for(i = 0; i < 16; ++i)
    {
        u16 rgb = inputPal[i];
        struct HSV hsv = RGBtoHSV(rgb);

        if(!IsColourExemptFromHueTint(i, hsv))
        {
            u8 currDist = GetHueDistance(targetHue, hsv.h);
            if(currDist > maxDist)
            {
                hue = hsv.h;
                maxDist = currDist;
            }
        }
    }

    return hue;
}

static u8 ModifyHue(s32 currHue, s32 averageHue, s32 targetHue)
{
    s32 delta = averageHue - currHue;
    s32 outHue = targetHue + delta;

    if(outHue < 0)
    {
        outHue += 255;
    }

    return outHue % 255;
}

static bool8 InsertionSortPlaceBefore(u8 elemA, u8 elemB, u8 targetHue)
{
    u8 distA = GetHueDistance(elemA, targetHue);
    u8 distB = GetHueDistance(elemB, targetHue);
    return distA < distB;
}

static void InsertionSort(u8 elem, u8* buffer, u8 currBufferCount, u8 targetHue)
{
    if(currBufferCount == 0)
    {
        // Insert remaining item at the end
        buffer[currBufferCount] = elem;
    }
    else if(currBufferCount == 1)
    {
        if(InsertionSortPlaceBefore(elem, buffer[0], targetHue))
        {
            buffer[currBufferCount] = buffer[0];
            buffer[0] = elem;
        }
        else
        {
            buffer[currBufferCount] = elem;
        }
    }
    else
    {
        u16 index = 0;
        u16 minIndex = 0;
        u16 maxIndex = currBufferCount - 1;

        // Insert sort, find the index to insert at
        while(minIndex != maxIndex)
        {
            AGB_ASSERT(minIndex < maxIndex);

            index = (maxIndex + minIndex) / 2;

            if(InsertionSortPlaceBefore(elem, buffer[index], targetHue))
            {
                if(maxIndex == index)
                    --maxIndex;
                else
                    maxIndex = index;
            }
            else
            {
                if(minIndex == index)
                    ++minIndex;
                else
                    minIndex = index;
            }
        }

        AGB_ASSERT(minIndex == maxIndex);

        // Special case to sort the end of the list
        if(minIndex == currBufferCount - 1)
        {
            if(InsertionSortPlaceBefore(elem, buffer[currBufferCount - 1], targetHue))
            {
                buffer[currBufferCount] = buffer[currBufferCount - 1];
                buffer[currBufferCount - 1] = elem;
            }
            else
            {
                buffer[currBufferCount] = elem;
            }
        }
        else
        {

            // Shift everything up
            for(index = currBufferCount; TRUE; --index)
            {
                buffer[index] = buffer[index - 1];

                if(index == minIndex + 1)
                    break;
            }

            buffer[minIndex] = elem;
        }
    }
}

static void CalculateNearFarHues(u16 const* inputPal, u8 avgHue, u8* nearHue, u8* farHue)
{
    u8 i;
    u8 sortedHues[16];
    u8 count = 0;

    for(i = 0; i < 16; ++i)
    {
        u16 rgb = inputPal[i];
        struct HSV hsv = RGBtoHSV(rgb);

        if(!IsColourExemptFromHueTint(i, hsv))
        {
            InsertionSort(hsv.h, sortedHues, count++, avgHue);
        }
    }

  if(count == 0)
{
    *nearHue = avgHue;
    *farHue = avgHue;
}
else
{
    *nearHue = sortedHues[0];
    *farHue = sortedHues[count - 1];
 }
}

void Rogue_GenerateLayerPaletteByHue(u16 const* inputPal, u16 const* layerRefPal, u16* outputLayers)
{
    u8 i;
    u8 nearestHue, furthestHue;
    u8 avgHue = GetAverageTintHue(layerRefPal);
    //u8 nearestHue = GetNearestTintHue(inputPal, avgHue);
    //u8 furthestHue = GetFurthestTintHue(inputPal, nearestHue);

    CalculateNearFarHues(layerRefPal, avgHue, &nearestHue, &furthestHue);

    for(i = 0; i < 16; ++i) // assume 16 palette slots (We currently don't have any use cases outside of this anyway)
    {
        u16 rgb = inputPal[i];
        struct HSV hsv = RGBtoHSV(rgb);

        if(IsColourExemptFromHueTint(i, hsv))
        {
            // Don't modify
            outputLayers[i] = RGB_BLACK;
        }
        else
        {
            u8 nearDist = GetHueDistance(hsv.h, nearestHue);
            u8 farDist = GetHueDistance(hsv.h, furthestHue);
            bool8 isPrimaryPal = nearDist < farDist;

            outputLayers[i] = isPrimaryPal ? RGB_RED : RGB_GREEN;
        }
    }

}

static u16 GreyScaleColour(u16 colour)
{
    u8 brightness = max(GET_R(colour), max(GET_G(colour), GET_B(colour)));
    return RGB(brightness, brightness, brightness);
}

static u16 CalculateWhitePointFor(u16 layerMask, const u16* basePal, const u16* layerPal)
{
    u16 layerWhitePoint = RGB(0, 0, 0);

    // Check if this layer is supported for this outfit
    if(layerMask != RGB(0, 0, 0))
    {
        u8 i;
        u16 baseCol;
        u16 layerCol;
        u16 currBrightness;
        u16 maxBrightness;

        // Calculate the average base colour in this layer
        maxBrightness = 0;

        for(i = 0; i < 16; ++i)
        {
            baseCol = basePal[i];
            layerCol = layerPal[i];

            if(layerCol == layerMask)
            {
                currBrightness = max(GET_R(baseCol), max(GET_G(baseCol), GET_B(baseCol)));

                if(maxBrightness == 0 || currBrightness > maxBrightness)
                {
                    maxBrightness = currBrightness;
                    layerWhitePoint = baseCol;
                }
            }
        }
    }

    return layerWhitePoint;
}

static bool8 ShouldModifyColourLayer(u16 chosenColour)
{
    // If alpha, just use input colour
    if((chosenColour & RGB_ALPHA) != 0)
        return FALSE;

    return TRUE;
}

#define COLOR_TRANSFORM_MULTIPLY_CHANNEL(value, whitePoint, target) min(31, ((((u32)value) * (u32)target) / (u32)whitePoint))

static u16 ModifyColourLayerMultiply(u16 chosenColour, u16 layerWhitePoint, u16 inputColour)
{
    u8 r, g, b;

    inputColour = GreyScaleColour(inputColour);
    r = GET_R(inputColour);
    g = GET_G(inputColour);
    b = GET_B(inputColour);

    r = COLOR_TRANSFORM_MULTIPLY_CHANNEL(r, GET_R(layerWhitePoint), GET_R(chosenColour));
    g = COLOR_TRANSFORM_MULTIPLY_CHANNEL(g, GET_G(layerWhitePoint), GET_G(chosenColour));
    b = COLOR_TRANSFORM_MULTIPLY_CHANNEL(b, GET_B(layerWhitePoint), GET_B(chosenColour));

    return RGB(r, g, b);
}

static u16 ModifyColourLayerHueShift(u16 chosenColour, u16 layerWhitePoint, u16 inputColour)
{
    struct HSV inputHSV = RGBtoHSV(inputColour);
    struct HSV chosenHSV = RGBtoHSV(chosenColour);
    struct HSV whitePointHSV = RGBtoHSV(layerWhitePoint);
    s32 s = inputHSV.s;
    s32 sDelta = (s32)chosenHSV.s - (s32)whitePointHSV.s;
    s32 v = inputHSV.v;
    s32 vDelta = (s32)chosenHSV.v - (s32)whitePointHSV.v;
    
    inputHSV.h -= whitePointHSV.h;
    inputHSV.h += chosenHSV.h;

    s += sDelta;
    inputHSV.s = min(255, max(0, s));
    v += vDelta;
    inputHSV.v = min(255, max(0, v));

    return HSVToRGB(inputHSV);
}

#undef COLOR_TRANSFORM_MULTIPLY_CHANNEL

void Rogue_GenerateWhitePointsPerLayers_Greyscale(u16 const* basePal, u16 const* layerPal, u16* layerWhitePoints, u16 const* layerMasks)
{
    u8 l;
    for(l = 0; l < PALETTE_MODIFY_LAYER_COUNT; ++l)
    {
        layerWhitePoints[l] = GreyScaleColour(CalculateWhitePointFor(layerMasks[l], basePal, layerPal));
    }
}

void Rogue_GenerateWhitePointsPerLayers(u16 const* basePal, u16 const* layerPal, u16* layerWhitePoints, u16 const* layerMasks)
{
    u8 l;
    for(l = 0; l < PALETTE_MODIFY_LAYER_COUNT; ++l)
    {
        layerWhitePoints[l] = CalculateWhitePointFor(layerMasks[l], basePal, layerPal);
    }
}

void Rogue_ModifyPaletteByLayersMultiply(u16 const* basePal, u16 const* layerPal, u16 const* layerWhitePoints, u16* writeBuffer, u16 const* layerMasks, u16 const* chosenColours)
{
    // Apply the dynamic changes using the layer pal
    u8 i, l;
    u16 baseCol, layerCol, layerMask;

    // Calculate each colour in the palette
    for(i = 0; i < 16; ++i)
    {
        baseCol = basePal[i];
        layerCol = layerPal[i];

        for(l = 0; l < PALETTE_MODIFY_LAYER_COUNT; ++l)
        {
            layerMask = layerMasks[l];

            if(layerCol == layerMask && ShouldModifyColourLayer(chosenColours[l]) == TRUE)
            {
                // Expect the whitepoint to already be in greyscale
                baseCol = ModifyColourLayerMultiply(chosenColours[l], layerWhitePoints[l], baseCol);
                break;
            }
        }

        writeBuffer[i] = baseCol;
        //writeBuffer[i] = layerCol; // debug view
    }
}

void Rogue_ModifyPaletteByLayersHueShift(u16 const* basePal, u16 const* layerPal, u16 const* layerWhitePoints, u16* writeBuffer, u16 const* layerMasks, u16 const* chosenColours)
{
    // Apply the dynamic changes using the layer pal
    u8 i, l;
    u16 baseCol, layerCol, layerMask;

    // Calculate each colour in the palette
for(i = 0; i < 16; ++i)
{
    baseCol = basePal[i];
    layerCol = layerPal[i];

    // Preserve original skin tones.
    //if (IsSkinTone(baseCol))
    //{
     //   writeBuffer[i] = baseCol;
     //   continue;
    //}
    
    // Proteger el color de índice 2 (piel).
    //if (i == 2)
    //{
    //    continue;
    //}
    // Modificación del color

    for(l = 0; l < PALETTE_MODIFY_LAYER_COUNT; ++l)
        {
            layerMask = layerMasks[l];

            if(layerCol == layerMask && ShouldModifyColourLayer(chosenColours[l]) == TRUE)
            {
                // Expect the whitepoint to already be in greyscale
                baseCol = ModifyColourLayerHueShift(chosenColours[l], layerWhitePoints[l], baseCol);
                break;
            }
        }

        writeBuffer[i] = baseCol;

        if(RogueDebug_GetConfigToggle(DEBUG_TOGGLE_SPRITE_LAYERS))
        {
            writeBuffer[i] = layerCol;
        }
    }
}
