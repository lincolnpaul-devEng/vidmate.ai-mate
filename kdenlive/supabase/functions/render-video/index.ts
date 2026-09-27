import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.38.4'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

const MAX_RETRIES = 3
const PROCESSOR_URL = Deno.env.get('VIDEO_PROCESSOR_URL') || "https://your-deployed-processor.com"
const HF_TOKEN = Deno.env.get('HF_TOKEN')


serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  try {
    const url = new URL(req.url)
    console.log(`📡 [${req.method}] ${url.href}`)
    const jobId = url.searchParams.get('jobId')

    // Setup Supabase
    const supabaseClient = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // ✅ POLLING (GET) - Check job status
    if (req.method === 'GET' && jobId) {
      console.log(`📊 Polling job status: ${jobId}`)
      
      try {
        const statusRes = await fetch(`${PROCESSOR_URL}/status/${jobId}`, {
          method: 'GET',
          headers: { 
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${HF_TOKEN}`
          }
        })

        if (!statusRes.ok) {
          // Processor might be down, return cached status from database
          const { data } = await supabaseClient
            .from('render_jobs')
            .select('*')
            .eq('job_id', jobId)
            .single()
          
          if (data) {
            return new Response(JSON.stringify({ render: data }), {
              headers: { ...corsHeaders, 'Content-Type': 'application/json' },
            })
          }
          
          return new Response(JSON.stringify({ 
            render: {
              status: 'ERROR', 
              error: 'Processor unreachable' 
            }
          }), {
            headers: { ...corsHeaders, 'Content-Type': 'application/json' },
            status: 503,
          })
        }

        const statusData = await statusRes.json()
        
        // ✅ Log status to database for persistence
        const dbUpdate: any = { 
          status: statusData.render?.status, 
          progress: statusData.render?.progress,
          updated_at: new Date().toISOString()
        };
        const outputUrl = statusData.render?.output_url || statusData.render?.presigned_url;
        if (outputUrl) {
          dbUpdate.output_url = outputUrl;
        }

        await supabaseClient
          .from('render_jobs')
          .update(dbUpdate)
          .eq('job_id', jobId)

        return new Response(JSON.stringify(statusData), {
          headers: { 
            ...corsHeaders, 
            'Content-Type': 'application/json',
            'Cache-Control': 'no-store'
          },
        })
      } catch (pollError) {
        console.error(`❌ Poll failed for ${jobId}:`, pollError)
        return new Response(JSON.stringify({ 
          render: {
            status: 'ERROR', 
            error: 'Failed to check job status',
            details: String(pollError)
          }
        }), {
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
          status: 500,
        })
      }
    }

    // ✅ START RENDER (POST) - Submit new job with retry logic
    if (req.method === 'POST') {
      const requestBody = await req.json()
      const { script, design, options, userId } = requestBody
      const generatedJobId = crypto.randomUUID()
      
      console.log(`🎬 Starting render job: ${generatedJobId}`)

      // ✅ Validate input: must have either script or design
      if (!script?.scenes && !design) {
        return new Response(JSON.stringify({ 
          success: false,
          error: 'Invalid input: must have either script with scenes or design'
        }), {
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
          status: 400
        })
      }

      if (script?.scenes && script.scenes.length > 50) {
        return new Response(JSON.stringify({ 
          success: false,
          error: 'Too many scenes: maximum 50'
        }), {
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
          status: 400
        })
      }

      // ✅ Check processor health before submitting
      try {
        const healthRes = await fetch(`${PROCESSOR_URL}/health`, {
          method: 'GET',
          headers: {
            'Authorization': `Bearer ${HF_TOKEN}`
          },
          signal: AbortSignal.timeout(5000)
        }).catch(() => null)

        if (!healthRes?.ok) {
          throw new Error('Video processor is not available')
        }
      } catch (e) {
        console.error('❌ Processor health check failed:', e)
        return new Response(JSON.stringify({ 
          success: false,
          error: 'Video processor is not available. Please try again later.',
          details: String(e)
        }), {
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
          status: 503
        })
      }

      // ✅ Create job record in database
      // If script is null, insert an empty script object because the DB column is JSONB NOT NULL
      const dbScript = script || { scenes: [] };
      const { error: dbError } = await supabaseClient
        .from('render_jobs')
        .insert({
          job_id: generatedJobId,
          user_id: userId || null,
          script: dbScript,
          design: design || null,
          options,
          status: 'PENDING',
          progress: 0,
          retry_count: 0,
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString()
        })

      if (dbError) {
        console.error('❌ Database error:', dbError)
        return new Response(JSON.stringify({ 
          success: false,
          error: 'Failed to create render job',
          details: dbError.message
        }), {
          headers: { ...corsHeaders, 'Content-Type': 'application/json' },
          status: 500
        })
      }

      // Determine processor endpoint and body content
      const isDesignRender = !!design;
      const endpoint = isDesignRender ? '/render' : '/script-render';
      
      // ✅ Submit to processor with retry logic
      let lastError = 'Unknown error'
      for (let attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        try {
          console.log(`📤 Submitting job to ${endpoint} (attempt ${attempt}/${MAX_RETRIES})`)
          
          const processorRes = await fetch(`${PROCESSOR_URL}${endpoint}`, {
            method: 'POST',
            headers: { 
              'Content-Type': 'application/json',
              'X-Job-ID': generatedJobId,
              'X-Attempt': String(attempt),
              'Authorization': `Bearer ${HF_TOKEN}`
            },
            body: JSON.stringify({ 
              script,
              design,
              options,
              jobId: generatedJobId,
              metadata: {
                source: 'socialpulse-web',
                userId: userId || 'anonymous',
                version: '1.0.0'
              }
            }),
            signal: AbortSignal.timeout(30000)
          })

          if (processorRes.ok) {
            const result = await processorRes.json()
            console.log(`✅ Job submitted successfully: ${generatedJobId}`)
            
            // Update DB to show job was submitted
            await supabaseClient
              .from('render_jobs')
              .update({ 
                status: 'PENDING',
                updated_at: new Date().toISOString()
              })
              .eq('job_id', generatedJobId)
            
            return new Response(JSON.stringify({ 
              success: true, 
              jobId: generatedJobId, 
              status: 'PENDING',
              pollUrl: `/functions/v1/render-video?jobId=${generatedJobId}`
            }), {
              headers: { ...corsHeaders, 'Content-Type': 'application/json' },
              status: 202,
            })
          }

          lastError = await processorRes.text()
          console.warn(`⚠️ Processor rejected (attempt ${attempt}): ${lastError}`)
          
          // Wait before retry (exponential backoff)
          await new Promise(resolve => setTimeout(resolve, 1000 * attempt))
          
        } catch (e) {
          lastError = String(e)
          console.error(`❌ Submission attempt ${attempt} failed:`, lastError)
          
          if (attempt < MAX_RETRIES) {
            await new Promise(resolve => setTimeout(resolve, 1000 * attempt))
          }
        }
      }

      // All retries failed
      const errorMsg = `Failed to submit job after ${MAX_RETRIES} attempts: ${lastError}`
      console.error(`🛑 ${errorMsg}`)
      
      // Update DB with failure
      await supabaseClient
        .from('render_jobs')
        .update({ 
          status: 'FAILED',
          error_message: errorMsg,
          updated_at: new Date().toISOString()
        })
        .eq('job_id', generatedJobId)

      return new Response(JSON.stringify({ 
        success: false, 
        error: errorMsg,
        jobId: generatedJobId
      }), {
        headers: { ...corsHeaders, 'Content-Type': 'application/json' },
        status: 503
      })
    }

    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 405
    })

  } catch (error) {
    console.error('❌ Render function error:', error)
    return new Response(JSON.stringify({ 
      success: false, 
      error: String(error),
      timestamp: new Date().toISOString()
    }), {
      headers: { ...corsHeaders, 'Content-Type': 'application/json' },
      status: 400
    })
  }
})
