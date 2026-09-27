import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.38.4'

const corsHeaders = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

serve(async (req) => {
  if (req.method === 'OPTIONS') {
    return new Response('ok', { headers: corsHeaders })
  }

  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      status: 405,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    })
  }

  try {
    const body = await req.json()
    
    const required = ['full_name', 'email', 'years_experience', 'tech_stack']
    for (const field of required) {
      if (!body[field]) {
        return new Response(JSON.stringify({ error: `Missing required field: ${field}` }), {
          status: 400,
          headers: { ...corsHeaders, 'Content-Type': 'application/json' }
        })
      }
    }

    const supabaseAdmin = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    const { data, error } = await supabaseAdmin
      .from('engineering_applications')
      .insert([{
        full_name: body.full_name,
        email: body.email,
        phone: body.phone || null,
        github_url: body.github_url || null,
        portfolio_url: body.portfolio_url || null,
        years_experience: body.years_experience,
        tech_stack: body.tech_stack || [],
        q1_system_design: body.q1_system_design || null,
        q2_pipeline_healing: body.q2_pipeline_healing || null,
        q3_pgvector_optimization: body.q3_pgvector_optimization || [],
        q3_pgvector_freetext: body.q3_pgvector_freetext || null,
        q4_webhook_reliability: body.q4_webhook_reliability || null,
        q5_monorepo_arch: body.q5_monorepo_arch || null,
        q6_hardest_problem: body.q6_hardest_problem || null,
        rating_video_processing: body.rating_video_processing || null,
        rating_ai_ml: body.rating_ai_ml || null,
        rating_system_design: body.rating_system_design || null,
        source: body.source || 'website',
        ip_address: body.ip_address || null,
        user_agent: body.user_agent || null,
        status: 'pending',
      }])
      .select()
      .single()

    if (error) {
      console.error('Insert error:', error)
      return new Response(JSON.stringify({ error: error.message }), {
        status: 500,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' }
      })
    }

    return new Response(JSON.stringify({ 
      success: true, 
      application_id: data.id,
      message: 'Application submitted successfully'
    }), {
      status: 201,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    })

  } catch (err) {
    console.error('Unexpected error:', err)
    return new Response(JSON.stringify({ error: 'Internal server error' }), {
      status: 500,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    })
  }
})
