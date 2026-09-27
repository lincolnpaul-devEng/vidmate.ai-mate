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
    const { application_id } = body

    if (!application_id) {
      return new Response(JSON.stringify({ error: 'Missing application_id' }), {
        status: 400,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' }
      })
    }

    const supabaseAdmin = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    )

    // Fetch application
    const { data: app, error: fetchError } = await supabaseAdmin
      .from('engineering_applications')
      .select('*')
      .eq('id', application_id)
      .single()

    if (fetchError || !app) {
      return new Response(JSON.stringify({ error: 'Application not found' }), {
        status: 404,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' }
      })
    }

    // Build prompt for AI evaluation
    const prompt = `You are a senior engineering recruiter evaluating a candidate for a full-stack AI video platform role. Score each dimension from 1-10 and provide a recommendation.

CANDIDATE PROFILE:
- Name: ${app.full_name}
- Experience: ${app.years_experience} years
- Tech Stack: ${app.tech_stack?.join(', ') || 'Not specified'}

RESPONSES:
Q1 - System Design (Video Render Pipeline): ${app.q1_system_design || 'No answer'}
Q2 - Self-Healing AI Pipeline: ${app.q2_pipeline_healing || 'No answer'}
Q3 - pgvector RAG Optimization: ${app.q3_pgvector_freetext || 'No answer'}
Q4 - Webhook Reliability at Scale: ${app.q4_webhook_reliability || 'No answer'}
Q5 - Monorepo Architecture: ${app.q5_monorepo_arch || 'No answer'}
Q6 - Hardest Technical Problem: ${app.q6_hardest_problem || 'No answer'}

SELF-ASSESSMENTS:
- Video Processing: ${app.rating_video_processing}/10
- AI/ML: ${app.rating_ai_ml}/10
- System Design: ${app.rating_system_design}/10

Respond in this exact JSON format (no markdown, no extra text):
{
  "technical_score": <number 1-10>,
  "communication_score": <number 1-10>,
  "problem_solving_score": <number 1-10>,
  "culture_fit_score": <number 1-10>,
  "overall_score": <number 1-10>,
  "recommendation": "<strong_yes|yes|maybe|no>",
  "summary": "<2-3 sentence summary of the candidate>",
  "strengths": ["<strength1>", "<strength2>", "<strength3>"],
  "concerns": ["<concern1>", "<concern2>"]
}`

    // Call OpenRouter for AI scoring
    const openRouterKey = Deno.env.get('OPENROUTER_API_KEY') ?? ''
    let aiResult

    if (openRouterKey) {
      try {
        const aiResponse = await fetch('https://openrouter.ai/api/v1/chat/completions', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${openRouterKey}`,
            'HTTP-Referer': 'https://tedoraltd.com',
            'X-Title': 'SocialPulse Hiring',
          },
          body: JSON.stringify({
            model: 'openrouter/owl-alpha',
            messages: [
              { role: 'system', content: 'You are a senior engineering recruiter. Respond ONLY with valid JSON.' },
              { role: 'user', content: prompt }
            ],
            temperature: 0.3,
            max_tokens: 500,
          }),
        })

        if (aiResponse.ok) {
          const aiData = await aiResponse.json()
          const content = aiData.choices?.[0]?.message?.content ?? ''
          // Parse JSON from response
          const jsonMatch = content.match(/\{[\s\S]*\}/)
          if (jsonMatch) {
            aiResult = JSON.parse(jsonMatch[0])
          }
        }
      } catch (aiErr) {
        console.error('OpenRouter error:', aiErr)
      }
    }

    // Fallback to algorithm if AI fails
    if (!aiResult) {
      const avgRating = ((app.rating_video_processing || 5) + (app.rating_ai_ml || 5) + (app.rating_system_design || 5)) / 3
      const qLengths = [app.q1_system_design, app.q2_pipeline_healing, app.q3_pgvector_freetext, app.q4_webhook_reliability, app.q5_monorepo_arch, app.q6_hardest_problem]
        .filter(Boolean).reduce((sum, q) => sum + (q?.length || 0), 0) / 6
      const techBonus = Math.min((app.tech_stack?.length || 0) * 0.3, 2)
      const score = Math.min(10, (avgRating * 0.35) + (Math.min(qLengths / 200, 1) * 10 * 0.35) + techBonus)
      
      let recommendation = 'maybe'
      if (score >= 7.5) recommendation = 'strong_yes'
      else if (score >= 6) recommendation = 'yes'
      else if (score >= 4) recommendation = 'maybe'
      else recommendation = 'no'

      aiResult = {
        technical_score: parseFloat((score * 0.9).toFixed(1)),
        communication_score: parseFloat((score * 0.8).toFixed(1)),
        problem_solving_score: parseFloat((score * 0.95).toFixed(1)),
        culture_fit_score: parseFloat((score * 0.7).toFixed(1)),
        overall_score: parseFloat(score.toFixed(1)),
        recommendation,
        summary: `Auto-scored: ${app.years_experience} exp, ${app.tech_stack?.length || 0} techs, avg rating ${avgRating.toFixed(1)}/10.`,
        strengths: app.tech_stack?.slice(0, 3) || [],
        concerns: score < 5 ? ['Low self-rating', 'Limited response depth'] : [],
      }
    }

    // Update scores in database
    const { error: updateError } = await supabaseAdmin.rpc('update_ai_scores', {
      p_application_id: application_id,
      p_overall: aiResult.overall_score,
      p_technical: aiResult.technical_score,
      p_communication: aiResult.communication_score,
      p_problem_solving: aiResult.problem_solving_score,
      p_culture_fit: aiResult.culture_fit_score,
      p_recommendation: aiResult.recommendation,
      p_summary: aiResult.summary,
      p_strengths: aiResult.strengths || [],
      p_concerns: aiResult.concerns || [],
    })

    if (updateError) {
      return new Response(JSON.stringify({ error: updateError.message }), {
        status: 500,
        headers: { ...corsHeaders, 'Content-Type': 'application/json' }
      })
    }

    return new Response(JSON.stringify({
      success: true,
      application_id,
      ai_overall_score: aiResult.overall_score,
      ai_recommendation: aiResult.recommendation,
      ai_powered: !!openRouterKey && aiResult !== undefined,
    }), {
      status: 200,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    })

  } catch (err) {
    console.error('Error:', err)
    return new Response(JSON.stringify({ error: 'Internal server error' }), {
      status: 500,
      headers: { ...corsHeaders, 'Content-Type': 'application/json' }
    })
  }
})
