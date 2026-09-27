import { serve } from "https://deno.land/std@0.168.0/http/server.ts";

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
};

type AssetType = 'videos' | 'sounds' | 'music' | 'stickers' | 'images';

interface PixabayVaultRequest {
  query: string;
  assetType: AssetType;
  perPage?: number;
}

interface PixabayAsset {
  id: string | number;
  previewUrl: string;
  downloadUrl: string;
  duration?: number;
  dimensions?: {
    width: number;
    height: number;
  };
}

interface PixabayVaultResponse {
  success: boolean;
  data?: PixabayAsset[];
  error?: string;
}

async function fetchPixabayAssets(
  query: string,
  assetType: AssetType,
  perPage: number = 20
): Promise<PixabayAsset[]> {
  const apiKey = Deno.env.get('PIXABAY_API_KEY');
  if (!apiKey) {
    throw new Error('PIXABAY_API_KEY not configured');
  }

  let endpoint = '';
  let params = new URLSearchParams({
    key: apiKey,
    q: query,
    per_page: Math.min(perPage, 200).toString(),
    order: 'popular',
    safesearch: 'true',
  });

  // Map asset type to Pixabay endpoints
  switch (assetType) {
    case 'videos':
      endpoint = 'https://pixabay.com/api/videos/';
      params.append('min_width', '640');
      params.append('min_height', '360');
      break;

    case 'sounds':
    case 'music':
      endpoint = 'https://pixabay.com/api/';
      params.append('type', 'all');
      params.append('media_type', 'audio');
      params.append('min_duration', '5');
      break;

    case 'images':
      endpoint = 'https://pixabay.com/api/';
      params.append('image_type', 'photo');
      params.append('orientation', 'all');
      break;

    case 'stickers':
      // Pixabay doesn't have a dedicated stickers endpoint
      // Use vector images as fallback
      endpoint = 'https://pixabay.com/api/';
      params.append('image_type', 'vector');
      break;

    default:
      throw new Error(`Unknown asset type: ${assetType}`);
  }

  const url = `${endpoint}?${params.toString()}`;

  const response = await fetch(url, {
    method: 'GET',
    headers: {
      'Accept': 'application/json',
    },
  });

  if (!response.ok) {
    throw new Error(`Pixabay API error: ${response.status} ${response.statusText}`);
  }

  const data = await response.json();

  if (!data.hits || !Array.isArray(data.hits)) {
    return [];
  }

  // Transform Pixabay response to standardized format
  const assets: PixabayAsset[] = data.hits.map((hit: any) => {
    const asset: PixabayAsset = {
      id: hit.id.toString(),
      previewUrl: '',
      downloadUrl: '',
    };

    // Handle different asset types
    if (assetType === 'videos') {
      const smallVideo = hit.videos?.small || hit.videos?.medium || hit.videos?.large;
      if (smallVideo) {
        asset.previewUrl = smallVideo.thumbnail || '';
        asset.downloadUrl = smallVideo.url || '';
        asset.duration = hit.duration || 0;
      }
    } else if (assetType === 'sounds' || assetType === 'music') {
      asset.previewUrl = hit.previewURL || '';
      asset.downloadUrl = hit.url || '';
      asset.duration = hit.duration || 0;
    } else {
      // Images and stickers
      asset.previewUrl = hit.previewURL || hit.thumbnail || '';
      asset.downloadUrl = hit.largeImageURL || hit.webformatURL || hit.pixelURL || hit.imageURL || '';
      if (hit.imageWidth && hit.imageHeight) {
        asset.dimensions = {
          width: hit.imageWidth,
          height: hit.imageHeight,
        };
      }
    }

    return asset;
  });

  return assets;
}

serve(async (req) => {
  // Handle CORS preflight
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders });
  }

  // Only accept POST requests
  if (req.method !== 'POST') {
    return new Response(
      JSON.stringify({ success: false, error: 'Method not allowed' }),
      {
        status: 405,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      }
    );
  }

  try {
    const body = await req.json() as PixabayVaultRequest;

    // Validate required fields
    if (!body.query || typeof body.query !== 'string') {
      return new Response(
        JSON.stringify({ success: false, error: 'Missing or invalid query parameter' }),
        {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        }
      );
    }

    if (!body.assetType || typeof body.assetType !== 'string') {
      return new Response(
        JSON.stringify({ success: false, error: 'Missing or invalid assetType parameter' }),
        {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        }
      );
    }

    const validAssetTypes: AssetType[] = ['videos', 'sounds', 'music', 'stickers', 'images'];
    if (!validAssetTypes.includes(body.assetType as AssetType)) {
      return new Response(
        JSON.stringify({
          success: false,
          error: `Invalid assetType. Must be one of: ${validAssetTypes.join(', ')}`,
        }),
        {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        }
      );
    }

    const perPage = body.perPage ? Math.max(3, Math.min(body.perPage, 200)) : 20;

    // Reject audio/music requests since Pixabay does not support it in the public API
    if (body.assetType === 'music' || body.assetType === 'sounds') {
      return new Response(
        JSON.stringify({
          success: false,
          error: 'Audio search is not supported by Pixabay API. Use Freesound for sound effects.',
        }),
        {
          status: 501,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        }
      );
    }

    // Fetch assets from Pixabay
    const assets = await fetchPixabayAssets(
      body.query,
      body.assetType as AssetType,
      perPage
    );

    const response: PixabayVaultResponse = {
      success: true,
      data: assets,
    };

    return new Response(JSON.stringify(response), {
      status: 200,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
    });
  } catch (error) {
    console.error('[pixabay-vault] Error:', error);

    const errorMessage = error instanceof Error ? error.message : 'Unknown error occurred';

    return new Response(
      JSON.stringify({
        success: false,
        error: errorMessage,
      }),
      {
        status: 500,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      }
    );
  }
});
